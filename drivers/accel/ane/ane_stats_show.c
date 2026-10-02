// SPDX-License-Identifier: GPL-2.0-only OR MIT
/*
 * sysfs/debugfs formatting shared by ane.ko and ane_t6021.ko so both
 * drivers expose identical files: same mode (0444), same fields, same
 * parser-friendly ASCII lines. The sysfs and debugfs plumbing stays
 * in each driver; both link this file.
 */

#include <linux/atomic.h>
#include <linux/seq_file.h>
#include <linux/sysfs.h>

#include "ane_stats.h"

/*
 * debugfs ane_timeline: the ring of recent submissions, one line per
 * committed slot:
 *   seq submit_ns start_ns end_ns tasks rc tmst
 * The field is a raw TM tick on ane.ko (unit unknown) and 0 on
 * ane_t6021 (no host TM path). A slot being written holds an odd
 * sequence and is skipped: the reader never blocks the producer and
 * never prints a torn line. The newest committed slot is printed
 * first; lines after wrap live in the same ring indices as their
 * tickets, so the seq value (2 * ticket) is unique.
 */
int ane_stats_timeline_show(struct seq_file *m, void *data)
{
	struct ane_stats_ring *ring = data;
	u64 head = atomic64_read(&ring->head);
	u64 live = head < (u64)ring->mask + 1 ? head : (u64)ring->mask + 1;

	seq_puts(m, "# ane_timeline: seq submit_ns start_ns end_ns tasks rc tmst (tmst raw tick on ane.ko, 0 = unavailable on ane_t6021)\n");
	/* Newest first. Submission ticket t lives in slot (t - 1) & mask
	 * and prints once complete() commits seq = 2*t; anything else
	 * (odd in-flight seq, stale slot) is skipped.
	 */
	for (u64 i = 0; i < live; i++) {
		u64 ticket = head - i;
		struct ane_stats_ring_entry *e =
			&ring->slots[(ticket - 1) & ring->mask];
		u64 seq = atomic64_read_acquire(&e->seq);
		u64 submit = atomic64_read(&e->submit_ns);
		u64 st = atomic64_read(&e->start_ns);
		u64 en = atomic64_read(&e->end_ns);
		u32 tasks = atomic_read(&e->tasks);
		u32 rc = atomic_read(&e->rc);
		u64 tmst = atomic64_read(&e->tmst);

		if (seq != 2 * ticket)
			continue; /* in flight or stale; never printed */
		seq_printf(m, "%llu %llu %llu %llu %u %u %llu\n",
			   (unsigned long long)(2 * ticket),
			   (unsigned long long)submit,
			   (unsigned long long)st,
			   (unsigned long long)en,
			   tasks, (unsigned int)rc,
			   (unsigned long long)tmst);
	}
	return 0;
}
