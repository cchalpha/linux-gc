#ifndef BFS_SCHED_H
#define BFS_SCHED_H

#include <linux/sched.h>
#include <linux/cpuidle.h>
#include <linux/stop_machine.h>
#include <linux/skip_list.h>

/*
 * This is the main, per-CPU runqueue data structure.
 * This data should only be modified by the local cpu.
 */
struct rq {
	/* runqueue lock: */
	raw_spinlock_t lock;

	struct task_struct *curr, *idle, *stop;
	struct mm_struct *prev_mm;

	struct skiplist_node sl_header;
	unsigned int nr_queued; /* number of tasks in skip list */

	/* switch count */
	u64 nr_switches;

	/*
	 * Stored data about rq->curr to work outside grq lock.
	 * TODO still need these local data?!
	 */
	u64 rq_deadline;
	bool rq_running; /* There is a task running */
	/* Accurate timekeeping data */
	u64 timekeep_clock;
	unsigned long user_pc, nice_pc, irq_pc, softirq_pc, system_pc,
		iowait_pc, idle_pc;
	atomic_t nr_iowait;

	int iso_ticks;
	bool iso_refractory;

#ifdef CONFIG_SMP
	int cpu;		/* cpu of this runqueue */
	bool online;
	bool scaling; /* This CPU is managed by a scaling CPU freq governor */

	struct root_domain *rd;
	struct sched_domain *sd;
#endif /* CONFIG_SMP */
#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	u64 prev_irq_time;
#endif /* CONFIG_IRQ_TIME_ACCOUNTING */
#ifdef CONFIG_PARAVIRT
	u64 prev_steal_time;
#endif /* CONFIG_PARAVIRT */
#ifdef CONFIG_PARAVIRT_TIME_ACCOUNTING
	u64 prev_steal_time_rq;
#endif /* CONFIG_PARAVIRT_TIME_ACCOUNTING */

	/* calc_load related fields */
	unsigned long calc_load_update;
	long calc_load_active;

	u64 clock, last_tick;
	u64 clock_task;
	int dither;

	unsigned long nr_running;
	unsigned long nr_uninterruptible;

	int last_tagged_queued_level;
	int last_running_policy_level;

#ifdef CONFIG_SCHEDSTATS

	/* latency stats */
	struct sched_info rq_sched_info;
	unsigned long long rq_cpu_time;
	/* could above be rq->cfs_rq.exec_clock + rq->rt_rq.rt_runtime ? */

	/* sys_sched_yield() stats */
	unsigned int yld_count;

	/* schedule() stats */
	unsigned int sched_switch;
	unsigned int sched_count;
	unsigned int sched_goidle;

	/* try_to_wake_up() stats */
	unsigned int ttwu_count;
	unsigned int ttwu_local;
#endif /* CONFIG_SCHEDSTATS */
#ifdef CONFIG_CPU_IDLE
	/* Must be inspected within a rcu lock section */
	struct cpuidle_state *idle_state;
#endif
};

extern unsigned long calc_load_update;
extern atomic_long_t calc_load_tasks;

extern void calc_global_load_tick(struct rq *this_rq);
extern long calc_load_fold_active(struct rq *this_rq, long adjust);

/*
#ifdef CONFIG_SMP
struct rq *cpu_rq(int cpu);
#endif
*/

#ifndef CONFIG_SMP
extern struct rq *uprq;
#define cpu_rq(cpu)	(uprq)
#define this_rq()	(uprq)
#define raw_rq()	(uprq)
#define task_rq(p)	(uprq)
#define cpu_curr(cpu)	((uprq)->curr)
#else /* CONFIG_SMP */
DECLARE_PER_CPU_SHARED_ALIGNED(struct rq, runqueues);
#define cpu_rq(cpu)		(&per_cpu(runqueues, (cpu)))
#define this_rq()		this_cpu_ptr(&runqueues)
#define raw_rq()		raw_cpu_ptr(&runqueues)
#define task_rq(p)		cpu_rq(task_cpu(p))
#define cpu_curr(cpu)		(cpu_rq(cpu)->curr)

#if defined(CONFIG_SCHED_DEBUG) && defined(CONFIG_SYSCTL)
void register_sched_domain_sysctl(void);
void unregister_sched_domain_sysctl(void);
#else
static inline void register_sched_domain_sysctl(void)
{
}
static inline void unregister_sched_domain_sysctl(void)
{
}
#endif

#endif /* CONFIG_SMP */

static inline u64 __rq_clock_broken(struct rq *rq)
{
	return READ_ONCE(rq->clock);
}

static inline u64 rq_clock(struct rq *rq)
{
	/*
	 * Relax lockdep_assert_held() checking as in VRQ, call to
	 * sched_info_xxxx() may not held rq->lock
	 * lockdep_assert_held(&rq->lock);
	 */
	return rq->clock;
}

static inline u64 rq_clock_task(struct rq *rq)
{
	/*
	 * Relax lockdep_assert_held() checking as in VRQ, call to
	 * sched_info_xxxx() may not held rq->lock
	 * lockdep_assert_held(&rq->lock);
	 */
	return rq->clock_task;
}

extern struct mutex sched_domains_mutex;
extern struct static_key_false sched_schedstats;

#define rcu_dereference_check_sched_domain(p) \
	rcu_dereference_check((p), \
			      lockdep_is_held(&sched_domains_mutex))

/*
 * The domain tree (rq->sd) is protected by RCU's quiescent state transition.
 * See detach_destroy_domains: synchronize_sched for details.
 *
 * The domain tree of any CPU may only be accessed from within
 * preempt-disabled sections.
 */
#define for_each_domain(cpu, __sd) \
	for (__sd = rcu_dereference_check_sched_domain(cpu_rq(cpu)->sd); __sd; __sd = __sd->parent)

static inline void sched_ttwu_pending(void) { }

#ifdef CONFIG_CPU_IDLE
static inline void idle_set_state(struct rq *rq,
				  struct cpuidle_state *idle_state)
{
	rq->idle_state = idle_state;
}

static inline struct cpuidle_state *idle_get_state(struct rq *rq)
{
	WARN_ON(!rcu_read_lock_held());
	return rq->idle_state;
}
#else
static inline void idle_set_state(struct rq *rq,
				  struct cpuidle_state *idle_state)
{
}

static inline struct cpuidle_state *idle_get_state(struct rq *rq)
{
	return NULL;
}
#endif

static inline int cpu_of(struct rq *rq)
{
#ifdef CONFIG_SMP
	return rq->cpu;
#else
	return 0;
#endif
}

#ifdef CONFIG_CPU_FREQ
DECLARE_PER_CPU(struct update_util_data *, cpufreq_update_util_data);

/**
 * cpufreq_update_util - Take a note about CPU utilization changes.
 * @rq: Runqueue to carry out the update for.
 * @flags: Update reason flags.
 *
 * This function is called by the scheduler on the CPU whose utilization is
 * being updated.
 *
 * It can only be called from RCU-sched read-side critical sections.
 *
 * The way cpufreq is currently arranged requires it to evaluate the CPU
 * performance state (frequency/voltage) on a regular basis to prevent it from
 * being stuck in a completely inadequate performance level for too long.
 * That is not guaranteed to happen if the updates are only triggered from CFS,
 * though, because they may not be coming in if RT or deadline tasks are active
 * all the time (or there are RT and DL tasks only).
 *
 * As a workaround for that issue, this function is called by the RT and DL
 * sched classes to trigger extra cpufreq updates to prevent it from stalling,
 * but that really is a band-aid.  Going forward it should be replaced with
 * solutions targeted more specifically at RT and DL tasks.
 */
static inline void cpufreq_update_util(struct rq *rq, unsigned int flags)
{
	struct update_util_data *data;

	data = rcu_dereference_sched(*this_cpu_ptr(&cpufreq_update_util_data));
	if (data)
		data->func(data, rq_clock(rq), flags);
}

static inline void cpufreq_update_this_cpu(struct rq *rq, unsigned int flags)
{
	if (cpu_of(rq) == smp_processor_id())
		cpufreq_update_util(rq, flags);
}
#else
static inline void cpufreq_update_util(struct rq *rq, unsigned int flags) {}
static inline void cpufreq_update_this_cpu(struct rq *rq, unsigned int flags) {}
#endif /* CONFIG_CPU_FREQ */

#ifdef CONFIG_SMP
#ifndef arch_scale_cpu_capacity
static __always_inline
unsigned long arch_scale_cpu_capacity(struct sched_domain *sd, int cpu)
{
	if (sd && (sd->flags & SD_SHARE_CPUCAPACITY) && (sd->span_weight > 1))
		return sd->smt_gain / sd->span_weight;

	return SCHED_CAPACITY_SCALE;
}
#endif
#endif

#ifdef arch_scale_freq_capacity
#ifndef arch_scale_freq_invariant
#define arch_scale_freq_invariant()	(true)
#endif
#else /* arch_scale_freq_capacity */
#define arch_scale_freq_invariant()	(false)
#endif

static inline void account_reset_rq(struct rq *rq)
{
#ifdef CONFIG_IRQ_TIME_ACCOUNTING
	rq->prev_irq_time = 0;
#endif
#ifdef CONFIG_PARAVIRT
	rq->prev_steal_time = 0;
#endif
#ifdef CONFIG_PARAVIRT_TIME_ACCOUNTING
	rq->prev_steal_time_rq = 0;
#endif
}

#endif /* BFS_SCHED_H */
