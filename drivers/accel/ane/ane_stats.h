/* SPDX-License-Identifier: GPL-2.0-only OR MIT */
/*
 * Producer-side ANE statistics shared by ane.ko (T8103/T6000) and
 * ane_t6021.ko (T602x/T8112): cumulative busy-time and job counters
 * plus a ring of recent submissions. The sysfs ane_stats file and the
 * debugfs ane_timeline file (formatting in ane_stats_show.c) exist
 * only when the module's `stats` parameter (default 1) is set; the
 * hot path then costs one predictable branch and atomics only -- no
 * allocation, no locking, no formatting.
 *
 * busy_ns counts nanoseconds the engine was busy since bind. For an
 * engine that serializes submissions (ane.ko, behind engine_lock) it
 * is the sum of the submit-to-completion windows; ane_t6021 can run
 * submissions in parallel, so it folds windows with the union rule
 * (see ane_stats_complete()). The show callback adds the live tail
 * of the still-open period, so the reported value advances while
 * engine work is in flight.
 */

#ifndef _ANE_STATS_H_
#define _ANE_STATS_H_

#include <linux/atomic.h>
#include <linux/ktime.h>
#include <linux/minmax.h>
#include <linux/seq_file.h>
#include <linux/string.h>
#include <linux/sysfs.h>
#include <linux/types.h>

/*
 * Transient close/open sentinel for ctrs->inflight. Never equal to a
 * real in-flight count, so a fold (last complete) and a period start
 * (first submit) cannot interleave: the cmpxchg loop spins while the
 * sentinel is observed and accepts nothing else.
 */
#define ANE_STATS_INFLIGHT_TRANS 0xFFFFFFFFu

/*
 * Per-device counters. busy_ns holds the busy time folded from closed
 * busy periods: a period opens when the first submission arrives on an
 * idle engine and closes when the last one completes, and its whole
 * span folds once at close. Overlapping submissions share the period,
 * so busy_ns is the union of the submit-to-completion intervals, not
 * their sum. The show callback adds the live tail of the still-open
 * period. jobs counts completed submissions. last_busy_end is the
 * open period's start timestamp; inflight is the number of in-flight
 * submissions, with ANE_STATS_INFLIGHT_TRANS as the transient close/
 * open sentinel.
 */
struct ane_stats_counters {
	atomic64_t	busy_ns;
	atomic64_t	jobs;
	atomic64_t	last_busy_end;
	atomic_t	inflight;
};

/*
 * Ring slot: seqlock-style per-slot sequence. Writer bumps seq at
 * entry (odd) and again on commit (even); reader loads begin/end
 * inside acquire load and the order relative to begin/end is preserved
 * by release. begin/end 0 means the slot is empty. submit_ns, end_ns,
 * tasks, rc, tmst are the diagnostic fields; tmst is the raw TM tick
 * on ane.ko (unknown unit) and 0 on ane_t6021 (unavailable, marked so
 * in the file header line).
 */
struct ane_stats_ring_entry {
	atomic64_t	seq;		/* even = committed; odd = writing */
	atomic64_t	submit_ns;
	atomic64_t	start_ns;
	atomic64_t	end_ns;
	atomic_t	tasks;
	atomic_t	rc;
	atomic64_t	tmst;		/* raw TM tick on ane.ko; 0 = unavailable */
};

/*
 * Ring container. head is the next ticket to issue (increments mod N).
 * N is a power of two preallocated at probe. slots is the array.
 * Reader takes begin/end under acquire and the seqlock protects
 * against torn writes. The ticket, not a slot index, identifies the
 * submission: slots are shared after wrap, tickets are not.
 */
struct ane_stats_ring {
	atomic64_t		head;
	u32			n;	/* power of two */
	u32			mask;	/* n - 1 */
	struct ane_stats_ring_entry *slots;
};

#define ANE_STATS_RING_ORDER_DEFAULT 8	/* 256 slots */
#define ANE_STATS_RING_ORDER_MAX     10	/* 1024 slots */

/*
 * Initialize counters and ring from zero. The ring slots array must
 * be preallocated (probe-time) and zeroed.
 */
static inline void ane_stats_counters_init(struct ane_stats_counters *ctrs,
					   struct ane_stats_ring *ring,
					   u32 n_shift)
{
	u32 n_total = 1u << n_shift;

	memset(ctrs, 0, sizeof(*ctrs));
	memset(ring, 0, sizeof(*ring));
	ring->n = n_total;
	ring->mask = n_total - 1u;
	atomic64_set_release(&ring->head, 0ull);
}

/*
 * Hot-path submission start. Caller holds the device's submission
 * serialization if the engine is single-producer (ane.ko); concurrent
 * drivers (ane_t6021) pass ring/ctrs and rely on the cmpxchg loops.
 * Returns the submission ticket (head + 1, starting at 1) for the
 * caller to later call ane_stats_complete() with.
 *
 * Counter side: the first submission onto an idle engine opens a busy
 * period by latching last_busy_end = submit_ns under the transition
 * sentinel; later overlapping submissions only increment inflight.
 * The sentinel window is a handful of instructions, so the bounded
 * cmpxchg retries never observe a half-open period.
 */
static inline u64 ane_stats_begin(struct ane_stats_counters *ctrs,
				  struct ane_stats_ring *ring,
				  u64 submit_ns, u32 tasks)
{
	u64 ticket = atomic64_fetch_add(1, &ring->head) + 1;
	struct ane_stats_ring_entry *e = &ring->slots[(ticket - 1) & ring->mask];
	u32 cur = atomic_read(&ctrs->inflight);

	for (;;) {
		if (cur == ANE_STATS_INFLIGHT_TRANS) {
			cur = atomic_read(&ctrs->inflight);
		} else if (!cur) {
			if (atomic_cmpxchg(&ctrs->inflight, 0,
					   ANE_STATS_INFLIGHT_TRANS) != 0) {
				cur = atomic_read(&ctrs->inflight);
				continue;
			}
			atomic64_set_release(&ctrs->last_busy_end, submit_ns);
			atomic_set_release(&ctrs->inflight, 1);
			break;
		} else if (atomic_try_cmpxchg(&ctrs->inflight, &cur, cur + 1)) {
			break;
		}
	}

	/* Order slot state before the writer sequence opens. */
	smp_wmb();
	(void)atomic64_read_acquire(&e->seq); /* pair with reader */
	/* In flight: seq stays odd (2*ticket - 1) until complete()
	 * commits the even final value 2*ticket. The reader only prints
	 * even seqs it can match, so an unfinished submission never
	 * prints and a torn write is never visible.
	 */
	atomic64_set_release(&e->seq, 2 * ticket - 1);
	atomic64_set_release(&e->submit_ns, submit_ns);
	atomic64_set_release(&e->start_ns, submit_ns);
	atomic64_set_release(&e->end_ns, submit_ns);
	atomic_set(&e->tasks, tasks);
	atomic_set(&e->rc, 0xFFFFFFFFU); /* sentinel: not done */
	atomic64_set_release(&e->tmst, 0);
	return ticket;
}

/*
 * Hot-path submission completion. The last submission out of a busy
 * period closes it: under the transition sentinel it folds the whole
 * period span [last_busy_end, end_ns] into busy_ns before reopening
 * the counter, so no reader can see a period both live and folded.
 * Overlapping submissions share the period, so busy_ns is the union
 * of the submit-to-completion intervals, not their sum. Updates the
 * slot's end_ns/rc/tmst and increments jobs.
 */
static inline void ane_stats_complete(struct ane_stats_counters *ctrs,
				      struct ane_stats_ring *ring,
				      u64 ticket, u64 end_ns,
				      u32 rc, u64 tmst)
{
	struct ane_stats_ring_entry *e = &ring->slots[(ticket - 1) & ring->mask];
	u32 cur = atomic_read(&ctrs->inflight);

	for (;;) {
		if (cur == ANE_STATS_INFLIGHT_TRANS) {
			cur = atomic_read(&ctrs->inflight);
		} else if (cur > 1) {
			if (atomic_try_cmpxchg(&ctrs->inflight, &cur, cur - 1))
				break;
		} else if (!cur) {
			/* Unbalanced complete (cannot happen with the
			 * documented one-begin-per-complete pairing):
			 * count the job, fold nothing, never hang.
			 */
			break;
		} else if (atomic_cmpxchg(&ctrs->inflight, 1,
					  ANE_STATS_INFLIGHT_TRANS) == 1) {
			u64 s = atomic64_read(&ctrs->last_busy_end);

			if (end_ns > s)
				atomic64_add(end_ns - s, &ctrs->busy_ns);
			atomic_set_release(&ctrs->inflight, 0);
			break;
		} else {
			cur = atomic_read(&ctrs->inflight);
		}
	}
	atomic64_add(1, &ctrs->jobs);

	/* Commit ring slot with the even final seq 2*ticket. The value
	 * must not depend on the current ring->head: concurrent
	 * submissions (ane_t6021) advance it, and a head-derived seq
	 * would mislabel the slot.
	 */
	/* Publish completion data before updating the sequence. */
	smp_wmb();
	atomic64_set_release(&e->end_ns, end_ns);
	atomic_set(&e->rc, rc);
	atomic64_set_release(&e->tmst, tmst);
	/* Publish the completed slot before the final sequence. */
	smp_wmb();
	atomic64_set_release(&e->seq, 2 * ticket);
}

/*
 * Accounting rule (coreglass producer contract): jobs counts completed
 * submissions, one begin/complete pair per engine submission -- one
 * per ANE_SUBMIT on ane.ko (so libane ane_exec is 1 job and ane_exec_loop
 * with N iterations is N jobs) and one per firmware PROCEDURE_CALL
 * (CSNE_CMD_PROCEDURE_CALL) on ane_t6021.ko: that opcode is the only
 * engine work on the shared ane_rtclient_command path. The control-
 * plane exchanges that ride the same function -- LOAD_PROGRAM,
 * CREATE_PROCESS, CH_PROPERTY_WRITE, install-time CONFIG_GET -- and
 * the boot transport are not engine submissions and are not counted.
 * A submission that completes with an error still completes its
 * begin/complete pair, so the counter stays balanced.
 *
 * Consistent counter snapshot across the tiny close/open transition:
 * the transition sentinel is never accepted, and (inflight, busy_ns)
 * must read back unchanged, so a value is only reported between
 * transitions. The value is busy_ns plus the live tail of the
 * still-open period, so it advances while engine work is in flight.
 * Over the timeline the reported value is non-decreasing: folds only
 * add, and at a close the live tail equals the fold.
 */
static inline u64 ane_stats_snapshot(const struct ane_stats_counters *ctrs,
				     u64 now)
{
	u64 raw, busy;
	u32 inflight;

	for (;;) {
		inflight = atomic_read_acquire(&ctrs->inflight);
		if (inflight == ANE_STATS_INFLIGHT_TRANS)
			continue; /* close/open in progress: spin it out */
		raw = atomic64_read(&ctrs->busy_ns);
		busy = raw;
		if (inflight) {
			u64 s = atomic64_read(&ctrs->last_busy_end);

			if (now > s)
				busy += now - s;
		}
		if (atomic_read(&ctrs->inflight) != inflight ||
		    atomic64_read(&ctrs->busy_ns) != raw)
			continue;
		break;
	}
	return busy;
}

/*
 * Typed sysfs formatter for the ane_stats attribute. The per-driver
 * show callbacks fetch the counters from their real drvdata type
 * (struct ane_device * on ane.ko, struct ane_rtclient * on
 * ane_t6021.ko) and pass &...->stats_ctrs here. The formatter never
 * sees the device pointer, so the drvdata type confusion that shipped
 * in the first round (reading the head of ane_device as counters)
 * cannot compile again: there is no cast to remove.
 */
static inline ssize_t ane_stats_emit(const struct ane_stats_counters *ctrs,
				     char *buf)
{
	return sysfs_emit(buf, "busy_ns %llu\njobs %llu\n",
			  (unsigned long long)ane_stats_snapshot(ctrs, ktime_get_ns()),
			  (unsigned long long)atomic64_read(&ctrs->jobs));
}

int ane_stats_timeline_show(struct seq_file *m, void *data);

#endif /* _ANE_STATS_H_ */
