/*
 * kernel/sched/bfs_debug.c
 *
 * BFS debug rotines
 *
 * Copyright(C) 2016, Alfred Chen
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/mempolicy.h>
#include <linux/binfmts.h>
#include <linux/debugfs.h>

#include "bfs_sched.h"

#ifdef CONFIG_SMP

#ifdef CONFIG_SYSCTL

static struct ctl_table sd_ctl_dir[] = {
	{
		.procname	= "sched_domain",
		.mode		= 0555,
	},
	{}
};

static struct ctl_table sd_ctl_root[] = {
	{
		.procname	= "kernel",
		.mode		= 0555,
		.child		= sd_ctl_dir,
	},
	{}
};

static struct ctl_table *sd_alloc_ctl_entry(int n)
{
	struct ctl_table *entry =
		kcalloc(n, sizeof(struct ctl_table), GFP_KERNEL);

	return entry;
}

static void sd_free_ctl_entry(struct ctl_table **tablep)
{
	struct ctl_table *entry;

	/*
	 * In the intermediate directories, both the child directory and
	 * procname are dynamically allocated and could fail but the mode
	 * will always be set. In the lowest directory the names are
	 * static strings and all have proc handlers.
	 */
	for (entry = *tablep; entry->mode; entry++) {
		if (entry->child)
			sd_free_ctl_entry(&entry->child);
		if (entry->proc_handler == NULL)
			kfree(entry->procname);
	}

	kfree(*tablep);
	*tablep = NULL;
}

/* Currently, BFS is not support load_idx */
/*
static int min_load_idx = 0;
static int max_load_idx = CPU_LOAD_IDX_MAX-1;
*/

static void
set_table_entry(struct ctl_table *entry,
		const char *procname, void *data, int maxlen,
		umode_t mode, proc_handler *proc_handler,
		bool load_idx)
{
	entry->procname = procname;
	entry->data = data;
	entry->maxlen = maxlen;
	entry->mode = mode;
	entry->proc_handler = proc_handler;

/*
	if (load_idx) {
		entry->extra1 = &min_load_idx;
		entry->extra2 = &max_load_idx;
	}
*/
}

static struct ctl_table *
sd_alloc_ctl_domain_table(struct sched_domain *sd)
{
	struct ctl_table *table = sd_alloc_ctl_entry(14);

	if (table == NULL)
		return NULL;

	set_table_entry(&table[0], "min_interval", &sd->min_interval,
		sizeof(long), 0644, proc_doulongvec_minmax, false);
	set_table_entry(&table[1], "max_interval", &sd->max_interval,
		sizeof(long), 0644, proc_doulongvec_minmax, false);
	set_table_entry(&table[2], "busy_idx", &sd->busy_idx,
		sizeof(int), 0644, proc_dointvec_minmax, true);
	set_table_entry(&table[3], "idle_idx", &sd->idle_idx,
		sizeof(int), 0644, proc_dointvec_minmax, true);
	set_table_entry(&table[4], "newidle_idx", &sd->newidle_idx,
		sizeof(int), 0644, proc_dointvec_minmax, true);
	set_table_entry(&table[5], "wake_idx", &sd->wake_idx,
		sizeof(int), 0644, proc_dointvec_minmax, true);
	set_table_entry(&table[6], "forkexec_idx", &sd->forkexec_idx,
		sizeof(int), 0644, proc_dointvec_minmax, true);
	set_table_entry(&table[7], "busy_factor", &sd->busy_factor,
		sizeof(int), 0644, proc_dointvec_minmax, false);
	set_table_entry(&table[8], "imbalance_pct", &sd->imbalance_pct,
		sizeof(int), 0644, proc_dointvec_minmax, false);
	set_table_entry(&table[9], "cache_nice_tries",
		&sd->cache_nice_tries,
		sizeof(int), 0644, proc_dointvec_minmax, false);
	set_table_entry(&table[10], "flags", &sd->flags,
		sizeof(int), 0644, proc_dointvec_minmax, false);
	set_table_entry(&table[11], "max_newidle_lb_cost",
		&sd->max_newidle_lb_cost,
		sizeof(long), 0644, proc_doulongvec_minmax, false);
	set_table_entry(&table[12], "name", sd->name,
		CORENAME_MAX_SIZE, 0444, proc_dostring, false);
	/* &table[13] is terminator */

	return table;
}

static struct ctl_table *sd_alloc_ctl_cpu_table(int cpu)
{
	struct ctl_table *entry, *table;
	struct sched_domain *sd;
	int domain_num = 0, i;
	char buf[32];

	for_each_domain(cpu, sd)
		domain_num++;
	entry = table = sd_alloc_ctl_entry(domain_num + 1);
	if (table == NULL)
		return NULL;

	i = 0;
	for_each_domain(cpu, sd) {
		snprintf(buf, 32, "domain%d", i);
		entry->procname = kstrdup(buf, GFP_KERNEL);
		entry->mode = 0555;
		entry->child = sd_alloc_ctl_domain_table(sd);
		entry++;
		i++;
	}
	return table;
}

static struct ctl_table_header *sd_sysctl_header;
void register_sched_domain_sysctl(void)
{
	int i, cpu_num = num_possible_cpus();
	struct ctl_table *entry = sd_alloc_ctl_entry(cpu_num + 1);
	char buf[32];

	WARN_ON(sd_ctl_dir[0].child);
	sd_ctl_dir[0].child = entry;

	if (entry == NULL)
		return;

	for_each_possible_cpu(i) {
		snprintf(buf, 32, "cpu%d", i);
		entry->procname = kstrdup(buf, GFP_KERNEL);
		entry->mode = 0555;
		entry->child = sd_alloc_ctl_cpu_table(i);
		entry++;
	}

	WARN_ON(sd_sysctl_header);
	sd_sysctl_header = register_sysctl_table(sd_ctl_root);
}

/* may be called multiple times per register */
void unregister_sched_domain_sysctl(void)
{
	unregister_sysctl_table(sd_sysctl_header);
	sd_sysctl_header = NULL;
	if (sd_ctl_dir[0].child)
		sd_free_ctl_entry(&sd_ctl_dir[0].child);
}
#endif /* CONFIG_SYSCTL */
#endif /* CONFIG_SMP */
