// SPDX-License-Identifier: GPL-2.0-only
#include <kunit/test.h>
#include <linux/errno.h>
#include <linux/module.h>

#include "kexec_internal.h"

struct kexec_block_test_state {
	unsigned int acquisitions;
};

static void kexec_block_test_cleanup(void *data)
{
	struct kexec_block_test_state *state = data;

	while (state->acquisitions) {
		kexec_unblock();
		state->acquisitions--;
	}
}

static int kexec_block_test_init(struct kunit *test)
{
	struct kexec_block_test_state *state;
	bool blocked;

	/* Never disturb a real device's retained-memory acquisition. */
	if (!kexec_trylock())
		kunit_skip(test, "a kexec operation is in progress");
	blocked = kexec_blocked();
	kexec_unlock();
	if (blocked)
		kunit_skip(test, "a device already blocks kexec");
	state = kunit_kzalloc(test, sizeof(*state), GFP_KERNEL);
	if (!state)
		return -ENOMEM;
	test->priv = state;
	return kunit_add_action_or_reset(test, kexec_block_test_cleanup, state);
}

static void kexec_test_acquire(struct kunit *test)
{
	struct kexec_block_test_state *state = test->priv;
	int ret = kexec_block();

	KUNIT_ASSERT_EQ(test, ret, 0);
	state->acquisitions++;
}

static void kexec_test_release(struct kunit *test)
{
	struct kexec_block_test_state *state = test->priv;

	KUNIT_ASSERT_GT(test, state->acquisitions, 0);
	kexec_unblock();
	state->acquisitions--;
}

static void kexec_test_expect_blocked(struct kunit *test, bool expected)
{
	bool blocked;

	KUNIT_ASSERT_TRUE(test, kexec_trylock());
	blocked = kexec_blocked();
	kexec_unlock();
	KUNIT_EXPECT_EQ(test, blocked, expected);
}

static void kexec_block_balanced_test(struct kunit *test)
{
	kexec_test_acquire(test);
	kexec_test_expect_blocked(test, true);
	kexec_test_release(test);
	kexec_test_expect_blocked(test, false);
}

static void kexec_block_nested_test(struct kunit *test)
{
	kexec_test_acquire(test);
	kexec_test_acquire(test);
	kexec_test_release(test);
	kexec_test_expect_blocked(test, true);
	kexec_test_release(test);
	kexec_test_expect_blocked(test, false);
}

static void kexec_block_busy_test(struct kunit *test)
{
	int ret;

	kexec_test_acquire(test);
	KUNIT_ASSERT_TRUE(test, kexec_trylock());
	ret = kexec_block();
	kexec_unlock();
	KUNIT_EXPECT_EQ(test, ret, -EBUSY);
	/* A failed acquisition must not leave an extra reference behind. */
	kexec_test_release(test);
	kexec_test_expect_blocked(test, false);
}

static void kexec_block_execution_test(struct kunit *test)
{
	bool image_loaded;

	kexec_test_acquire(test);
	KUNIT_ASSERT_TRUE(test, kexec_trylock());
	image_loaded = !!kexec_image;
	kexec_unlock();
	/*
	 * New loads are blocked during this test. Require an empty image so
	 * even a broken execution guard cannot transition to another kernel.
	 * The blocker must take precedence over the no-image -EINVAL result.
	 */
	if (image_loaded)
		kunit_skip(test, "a normal kexec image is already loaded");
	KUNIT_EXPECT_EQ(test, kernel_kexec(), -EBUSY);
	kexec_test_release(test);
}

static struct kunit_case kexec_block_test_cases[] = {
	KUNIT_CASE(kexec_block_balanced_test),
	KUNIT_CASE(kexec_block_nested_test),
	KUNIT_CASE(kexec_block_busy_test),
	KUNIT_CASE(kexec_block_execution_test),
	{}
};

static struct kunit_suite kexec_block_test_suite = {
	.name = "kexec-block",
	.init = kexec_block_test_init,
	.test_cases = kexec_block_test_cases,
};

kunit_test_suite(kexec_block_test_suite);

MODULE_LICENSE("GPL");
