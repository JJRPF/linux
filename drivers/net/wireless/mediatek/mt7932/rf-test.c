// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>

#include "mt7932.h"

static void mt_rf_test_device_release(struct device *dev)
{
	/* The enclosing synthetic PCI device is owned by KUnit. */
}

static void mt_rf_test_cleanup(void *data)
{
	struct mt7932 *m = data;

	free_netdev(m->netdev);
	put_device(&m->pdev->dev);
}

static int mt_rf_test_init(struct kunit *test)
{
	struct mt7932 *m;
	int ret;

	m = kunit_kzalloc(test, sizeof(*m), GFP_KERNEL);
	if (!m)
		return -ENOMEM;
	m->pdev = kunit_kzalloc(test, sizeof(*m->pdev), GFP_KERNEL);
	if (!m->pdev)
		return -ENOMEM;
	device_initialize(&m->pdev->dev);
	m->pdev->dev.release = mt_rf_test_device_release;
	ret = dev_set_name(&m->pdev->dev, "mt7932-rf-test");
	if (ret) {
		put_device(&m->pdev->dev);
		return ret;
	}
	m->netdev = alloc_etherdev(0);
	if (!m->netdev) {
		put_device(&m->pdev->dev);
		return -ENOMEM;
	}
	ret = kunit_add_action_or_reset(test, mt_rf_test_cleanup, m);
	if (ret)
		return ret;
	spin_lock_init(&m->response_lock);
	spin_lock_init(&m->data_lock);
	init_completion(&m->cal_response);
	init_completion(&m->assoc_start);
	init_completion(&m->assoc_done);
	init_completion(&m->discovery_done);
	m->interface_registered = true;
	m->connecting = true;
	m->peer_valid = true;
	m->rf_ready = true;
	m->cal_state.active = true;
	netif_carrier_on(m->netdev);
	netif_start_queue(m->netdev);
	test->priv = m;
	return 0;
}

static void mt_rf_test_fail(struct mt7932 *m, int error)
{
	unsigned long flags;

	spin_lock_irqsave(&m->response_lock, flags);
	mt_rf_fail_locked(m, error);
	spin_unlock_irqrestore(&m->response_lock, flags);
}

static void mt_rf_failure_wakes_association_test(struct kunit *test)
{
	struct mt7932 *m = test->priv;

	/* Exercise the data-publisher join, without publishing any DMA. */
	smp_store_release(&m->data_ready, true);
	mt_rf_test_fail(m, -ENOENT);

	KUNIT_EXPECT_TRUE(test, completion_done(&m->cal_response));
	KUNIT_EXPECT_TRUE(test, completion_done(&m->assoc_start));
	KUNIT_EXPECT_TRUE(test, completion_done(&m->assoc_done));
	KUNIT_EXPECT_TRUE(test, completion_done(&m->discovery_done));
	KUNIT_EXPECT_EQ(test, m->connect_error, -ENOENT);
	KUNIT_EXPECT_FALSE(test, mt_rf_allowed(m));
	KUNIT_EXPECT_FALSE(test, netif_carrier_ok(m->netdev));
	KUNIT_EXPECT_TRUE(test, netif_queue_stopped(m->netdev));
	/* An error must not claim that the firmware-owned peer was retired. */
	KUNIT_EXPECT_TRUE(test, m->peer_valid);
}

static void mt_rf_failure_blocks_startup_test(struct kunit *test)
{
	struct mt7932 *m = test->priv;

	mt_rf_test_fail(m, -EPROTO);
	/* No BAR/DMA exists: startup must reject before touching hardware. */
	KUNIT_EXPECT_EQ(test, mt_enable_scan(m), -EPROTO);
	KUNIT_EXPECT_FALSE(test, m->rf_ready);
	KUNIT_EXPECT_FALSE(test, m->data_ready);
}

static void mt_rf_failure_preserves_first_error_test(struct kunit *test)
{
	struct mt7932 *m = test->priv;

	mt_rf_test_fail(m, -ENOENT);
	mt_rf_test_fail(m, -EPROTO);
	KUNIT_EXPECT_EQ(test, m->cal_state.error, -ENOENT);
	KUNIT_EXPECT_EQ(test, m->connect_error, -ENOENT);
	KUNIT_EXPECT_EQ(test, mt_enable_scan(m), -ENOENT);
}

static struct kunit_case mt_rf_test_cases[] = {
	KUNIT_CASE(mt_rf_failure_wakes_association_test),
	KUNIT_CASE(mt_rf_failure_blocks_startup_test),
	KUNIT_CASE(mt_rf_failure_preserves_first_error_test),
	{}
};

static struct kunit_suite mt_rf_test_suite = {
	.name = "mt7932-rf-failure",
	.init = mt_rf_test_init,
	.test_cases = mt_rf_test_cases,
};

kunit_test_suite(mt_rf_test_suite);
