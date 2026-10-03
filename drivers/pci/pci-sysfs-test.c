// SPDX-License-Identifier: GPL-2.0-only
/* Included by pci-sysfs.c to exercise its actual remove_store callback. */
#include <kunit/test.h>

struct pci_sysfs_test_hierarchy {
	struct pci_host_bridge bridge;
	struct pci_bus root_bus;
	struct pci_bus child_bus;
	struct pci_dev root_port;
	struct pci_dev endpoint;
};

static int pci_sysfs_test_init(struct kunit *test)
{
	struct pci_sysfs_test_hierarchy *hierarchy;

	hierarchy = kunit_kzalloc(test, sizeof(*hierarchy), GFP_KERNEL);
	if (!hierarchy)
		return -ENOMEM;
	/*
	 * Only topology and diagnostic names are needed. These objects are never
	 * registered, referenced by the PCI core or passed to a removal routine.
	 * Every tested callback must return before device_remove_file_self().
	 */
	hierarchy->root_bus.bridge = &hierarchy->bridge.dev;
	hierarchy->child_bus.parent = &hierarchy->root_bus;
	hierarchy->root_port.bus = &hierarchy->root_bus;
	hierarchy->root_port.dev.init_name = "kunit-root-port";
	hierarchy->endpoint.bus = &hierarchy->child_bus;
	hierarchy->endpoint.dev.init_name = "kunit-endpoint";
	test->priv = hierarchy;
	return 0;
}

static void pci_sysfs_remove_blocked_root_test(struct kunit *test)
{
	struct pci_sysfs_test_hierarchy *hierarchy = test->priv;

	hierarchy->bridge.no_user_remove = true;
	KUNIT_EXPECT_EQ(test,
			remove_store(&hierarchy->root_port.dev, &dev_attr_remove, "1\n", 2),
			(ssize_t)-EBUSY);
}

static void pci_sysfs_remove_blocked_child_test(struct kunit *test)
{
	struct pci_sysfs_test_hierarchy *hierarchy = test->priv;

	hierarchy->bridge.no_user_remove = true;
	KUNIT_EXPECT_EQ(test,
			remove_store(&hierarchy->endpoint.dev, &dev_attr_remove, "1\n", 2),
			(ssize_t)-EBUSY);
}

static void pci_sysfs_remove_malformed_test(struct kunit *test)
{
	struct pci_sysfs_test_hierarchy *hierarchy = test->priv;

	KUNIT_EXPECT_EQ(test,
			remove_store(&hierarchy->endpoint.dev, &dev_attr_remove, "bad\n", 4),
			(ssize_t)-EINVAL);
}

static void pci_sysfs_remove_zero_blocked_test(struct kunit *test)
{
	struct pci_sysfs_test_hierarchy *hierarchy = test->priv;

	hierarchy->bridge.no_user_remove = true;
	KUNIT_EXPECT_EQ(test,
			remove_store(&hierarchy->endpoint.dev, &dev_attr_remove, "0\n", 2),
			(ssize_t)2);
}

static void pci_sysfs_remove_zero_default_test(struct kunit *test)
{
	struct pci_sysfs_test_hierarchy *hierarchy = test->priv;

	KUNIT_EXPECT_EQ(test,
			remove_store(&hierarchy->root_port.dev, &dev_attr_remove, "0\n", 2),
			(ssize_t)2);
}

static struct kunit_case pci_sysfs_test_cases[] = {
	KUNIT_CASE(pci_sysfs_remove_blocked_root_test),
	KUNIT_CASE(pci_sysfs_remove_blocked_child_test),
	KUNIT_CASE(pci_sysfs_remove_malformed_test),
	KUNIT_CASE(pci_sysfs_remove_zero_blocked_test),
	KUNIT_CASE(pci_sysfs_remove_zero_default_test),
	{}
};

static struct kunit_suite pci_sysfs_test_suite = {
	.name = "pci-sysfs-remove",
	.init = pci_sysfs_test_init,
	.test_cases = pci_sysfs_test_cases,
};

kunit_test_suite(pci_sysfs_test_suite);
