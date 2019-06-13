/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2011-2014,2016                           Inria
 * Copyright (C) 2013                                     Simon Archipoff
 * Copyright (C) 2009-2018                                Université de Bordeaux
 * Copyright (C) 2013                                     Joris Pablo
 * Copyright (C) 2010-2018                                CNRS
 * Copyright (C) 2011                                     Télécom-SudParis
 *
 * StarPU is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * StarPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

/* Distributed queues using performance modeling to assign tasks */

#include <starpu_config.h>
#include <starpu_scheduler.h>

#include <common/fxt.h>
#include <core/task.h>
#include <core/sched_policy.h>
#include <core/debug.h>

#include <sched_policies/fifo_queues.h>
#include <limits.h>

// #include "fifo_queues.h"
#include <common/thread.h>
#include <starpu_bitmap.h>
#include <pthread.h>

#include "SobolQRNG/sobol.h"
#include "SobolQRNG/sobol_gold.h"
#include "pi.h"
#include <starpu.h>

#include <common/fxt.h>
#include <core/task.h>
#include <core/sched_policy.h>
#include <core/debug.h>

#include <sched_policies/fifo_queues.h>
#include <starpu_scheduler.h>
#include <limits.h>
#include <stdlib.h>

#ifndef DBL_MIN
#define DBL_MIN __DBL_MIN__
#endif

#ifndef DBL_MAX
#define DBL_MAX __DBL_MAX__
#endif


#define THRESHOLD 2 		/* not used yet */
#define MAX_TASK_LEN_INIT -1
#define MIN_TASK_LEN_INIT 999999999999 


#define FPRINTF(ofile, fmt, ...) do { if (!getenv("STARPU_SSILENT")) {fprintf(ofile, fmt, ## __VA_ARGS__); }} while(0)


unsigned task_count = 0;
starpu_pthread_mutex_t count_lock;


struct _starpu_dmda_data
{
	double alpha;
	double beta;
	double _gamma;
	double idle_power;

    struct starpu_task_list main_list;
    starpu_pthread_mutex_t policy_mutex;

	struct _starpu_fifo_taskq **queue_array;

	long int total_task_cnt;
	long int ready_task_cnt;
	long int eager_task_cnt; /* number of tasks scheduled without model */
	int num_priorities;
};

/* The dmda scheduling policy uses
 *
 * alpha * T_computation + beta * T_communication + gamma * Consumption
 *
 * Here are the default values of alpha, beta, gamma
 */

#define _STARPU_SCHED_ALPHA_DEFAULT 1.0
#define _STARPU_SCHED_BETA_DEFAULT 1.0
#define _STARPU_SCHED_GAMMA_DEFAULT 1000.0

#ifdef STARPU_USE_TOP
static double alpha = _STARPU_SCHED_ALPHA_DEFAULT;
static double beta = _STARPU_SCHED_BETA_DEFAULT;
static double _gamma = _STARPU_SCHED_GAMMA_DEFAULT;
static double idle_power = 0.0;
static const float alpha_minimum=0;
static const float alpha_maximum=10.0;
static const float beta_minimum=0;
static const float beta_maximum=10.0;
static const float gamma_minimum=0;
static const float gamma_maximum=10000.0;
static const float idle_power_minimum=0;
static const float idle_power_maximum=10000.0;
#endif /* !STARPU_USE_TOP */

unsigned _sched_ctx_id;

static int count_non_ready_buffers(struct starpu_task *task, unsigned node)
{
	int cnt = 0;
	unsigned nbuffers = STARPU_TASK_GET_NBUFFERS(task);
	unsigned index;

	for (index = 0; index < nbuffers; index++)
	{
		starpu_data_handle_t handle;
		unsigned buffer_node = node;
		if (task->cl->specific_nodes)
			buffer_node = STARPU_CODELET_GET_NODE(task->cl, index);

		handle = STARPU_TASK_GET_HANDLE(task, index);

		int is_valid;
		starpu_data_query_status(handle, buffer_node, NULL, &is_valid, NULL);

		if (!is_valid)
			cnt++;
	}

	return cnt;
}

#ifdef STARPU_USE_TOP
static void param_modified(struct starpu_top_param* d)
{
#ifdef STARPU_DEVEL
#warning FIXME: get sched ctx to get alpha/beta/gamma/idle values
#endif
	/* Just to show parameter modification. */
	_STARPU_MSG("%s has been modified : "
		    "alpha=%f|beta=%f|gamma=%f|idle_power=%f !\n",
		    d->name, alpha,beta,_gamma, idle_power);
}
#endif /* !STARPU_USE_TOP */

static int _normalize_prio(int priority, int num_priorities, unsigned sched_ctx_id)
{
	int min = starpu_sched_ctx_get_min_priority(sched_ctx_id);
	int max = starpu_sched_ctx_get_max_priority(sched_ctx_id);
	return ((num_priorities-1)/(max-min)) * (priority - min);
}

static struct starpu_task *dmda_pop_task(unsigned sched_ctx_id)
{
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	// STARPU_PTHREAD_MUTEX_LOCK (&dt->policy_mutex);
	struct starpu_task *task;

	unsigned workerid = starpu_worker_get_id_check();
	struct _starpu_fifo_taskq *fifo = dt->queue_array[workerid];

	/* Take the opportunity to update start time */
	fifo->exp_start = STARPU_MAX(starpu_timing_now(), fifo->exp_start);
	fifo->exp_end = fifo->exp_start + fifo->exp_len;

	STARPU_ASSERT_MSG(fifo, "worker %u does not belong to ctx %u anymore.\n", workerid, sched_ctx_id);

	task = _starpu_fifo_pop_local_task(fifo);
	// STARPU_PTHREAD_MUTEX_UNLOCK (&dt->policy_mutex);
	if (task)
	{
#ifdef STARPU_VERBOSE
		if (task->cl)
		{
			int non_ready = count_non_ready_buffers(task, starpu_worker_get_memory_node(workerid));
			if (non_ready == 0)
				dt->ready_task_cnt++;
		}
		
		dt->total_task_cnt++;
#endif

    // STARPU_PTHREAD_MUTEX_LOCK (&count_lock);
    task_count--;
    // STARPU_PTHREAD_MUTEX_UNLOCK (&count_lock);
	}
	return task;
}

static struct starpu_task *dmda_pop_every_task(unsigned sched_ctx_id)
{
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	struct starpu_task *new_list;

	unsigned workerid = starpu_worker_get_id_check();
	struct _starpu_fifo_taskq *fifo = dt->queue_array[workerid];

	/* Take the opportunity to update start time */
	fifo->exp_start = STARPU_MAX(starpu_timing_now(), fifo->exp_start);
	fifo->exp_end = fifo->exp_start + fifo->exp_len;

	starpu_pthread_mutex_t *sched_mutex;
	starpu_pthread_cond_t *sched_cond;
	starpu_worker_get_sched_condition(workerid, &sched_mutex, &sched_cond);
	// STARPU_PTHREAD_MUTEX_LOCK_SCHED(sched_mutex);
	new_list = _starpu_fifo_pop_every_task(fifo, workerid);
	// STARPU_PTHREAD_MUTEX_UNLOCK_SCHED(sched_mutex);
	return new_list;
}

static int push_task_on_best_worker(struct starpu_task *task, int best_workerid,
				    double predicted, double predicted_transfer,
				    int prio, unsigned sched_ctx_id)
{
	static int cou = 0;
	cou++;
	printf("%d\n", cou);
    // printf("e\n");
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	/* make sure someone coule execute that task ! */
	STARPU_ASSERT(best_workerid != -1);
	unsigned child_sched_ctx = starpu_sched_ctx_worker_is_master_for_child_ctx(best_workerid, sched_ctx_id);
        if(child_sched_ctx != STARPU_NMAX_SCHED_CTXS)
        {
		starpu_sched_ctx_revert_task_counters(sched_ctx_id, task->flops);
                starpu_sched_ctx_move_task_to_ctx(task, child_sched_ctx);
                return 0;
        }

	struct _starpu_fifo_taskq *fifo = dt->queue_array[best_workerid];

	starpu_pthread_mutex_t *sched_mutex;
	starpu_pthread_cond_t *sched_cond;
	starpu_worker_get_sched_condition(best_workerid, &sched_mutex, &sched_cond);

#ifdef STARPU_USE_SC_HYPERVISOR
	starpu_sched_ctx_call_pushed_task_cb(best_workerid, sched_ctx_id);
#endif //STARPU_USE_SC_HYPERVISOR


        /* Sometimes workers didn't take the tasks as early as we expected */
	fifo->exp_start = isnan(fifo->exp_start) ? starpu_timing_now() : STARPU_MAX(fifo->exp_start, starpu_timing_now());
	fifo->exp_end = fifo->exp_start + fifo->exp_len;
	fifo->exp_end = fifo->exp_start + fifo->exp_len;

	if ((starpu_timing_now() + predicted_transfer) < fifo->exp_end)
	{
		/* We may hope that the transfer will be finished by
		 * the start of the task. */
		predicted_transfer = 0.0;
	}
	else
	{
		/* The transfer will not be finished by then, take the
		 * remainder into account */
		predicted_transfer = (starpu_timing_now() + predicted_transfer) - fifo->exp_end;
	}

	if(!isnan(predicted_transfer)) 
	{
		fifo->exp_end += predicted_transfer;
		fifo->exp_len += predicted_transfer;
		if(dt->num_priorities != -1)
		{
			int i;
			int task_prio = _normalize_prio(task->priority, dt->num_priorities, task->sched_ctx);
			for(i = 0; i <= task_prio; i++)
				fifo->exp_len_per_priority[i] += predicted_transfer;
		}

	}

	if(!isnan(predicted))
	{
		fifo->exp_end += predicted;
		fifo->exp_len += predicted;
		if(dt->num_priorities != -1)
		{
			int i;
			int task_prio = _normalize_prio(task->priority, dt->num_priorities, task->sched_ctx);
			for(i = 0; i <= task_prio; i++)
				fifo->exp_len_per_priority[i] += predicted;
		}

	}


	task->predicted = predicted;
	task->predicted_transfer = predicted_transfer;

#ifdef STARPU_USE_TOP
	starpu_top_task_prevision(task, best_workerid,
				  (unsigned long long)(fifo->exp_end-predicted)/1000,
				  (unsigned long long)fifo->exp_end/1000);
#endif /* !STARPU_USE_TOP */

	if (starpu_get_prefetch_flag())
	{
		unsigned memory_node = starpu_worker_get_memory_node(best_workerid);
		starpu_prefetch_task_input_on_node(task, memory_node);
	}

	STARPU_AYU_ADDTOTASKQUEUE(starpu_task_get_job_id(task), best_workerid);
	int ret = 0;
	if (prio)
	{
		ret =_starpu_fifo_push_sorted_task(dt->queue_array[best_workerid], task);
		if(dt->num_priorities != -1)
		{
			int i;
			int task_prio = _normalize_prio(task->priority, dt->num_priorities, task->sched_ctx);
			for(i = 0; i <= task_prio; i++)
				dt->queue_array[best_workerid]->ntasks_per_priority[i]++;
		}


#if !defined(STARPU_NON_BLOCKING_DRIVERS) || defined(STARPU_SIMGRID)
		starpu_wakeup_worker_locked(best_workerid, sched_cond, sched_mutex);
#endif
		starpu_push_task_end(task);
	}
	else
	{
		starpu_task_list_push_back (&dt->queue_array[best_workerid]->taskq, task);
		dt->queue_array[best_workerid]->ntasks++;
		dt->queue_array[best_workerid]->nprocessed++;
#if !defined(STARPU_NON_BLOCKING_DRIVERS) || defined(STARPU_SIMGRID)
		starpu_wakeup_worker_locked(best_workerid, sched_cond, sched_mutex);
#endif
		starpu_push_task_end(task);
	}

	return ret;
}

static int compute_tasks_on_device_queues(unsigned sched_ctx_id){
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
    struct _starpu_fifo_taskq **queue_array = dt->queue_array;
	struct starpu_task *cur;
	int task_count_on_all_devices = 0;
	for (int i = 0; i < STARPU_NMAXWORKERS; i++){
        if (queue_array[i] != NULL){
            cur = queue_array[i]->taskq.head;
            while(cur != NULL){
                cur = cur->next;
                task_count_on_all_devices ++;
            }
        }
	}
	return task_count_on_all_devices;
}

static void compute_all_performance_predictions(struct starpu_task *task,
						unsigned nworkers,
						double local_task_length[nworkers][STARPU_MAXIMPLEMENTATIONS],
						double exp_end[nworkers][STARPU_MAXIMPLEMENTATIONS],
						double *max_exp_endp,
						double *best_exp_endp,
						double local_data_penalty[nworkers][STARPU_MAXIMPLEMENTATIONS],
						double local_energy[nworkers][STARPU_MAXIMPLEMENTATIONS],
						int *forced_worker, int *forced_impl, unsigned sched_ctx_id, unsigned sorted_decision)
{
	int calibrating = 0;
	double max_exp_end = DBL_MIN;
	double best_exp_end = DBL_MAX;
	int ntasks_best = -1;
	int nimpl_best = 0;
	double ntasks_best_end = 0.0;

	/* A priori, we know all estimations */
	int unknown = 0;
	unsigned worker, worker_ctx = 0;

	unsigned nimpl;
	unsigned impl_mask;
	int task_prio = 0;

	starpu_task_bundle_t bundle = task->bundle;
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	
	if(sorted_decision && dt->num_priorities != -1)
		task_prio = _normalize_prio(task->priority, dt->num_priorities, sched_ctx_id);

	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);

	struct starpu_sched_ctx_iterator it;

	workers->init_iterator(workers, &it);
	while(workers->has_next_master(workers, &it))
	{
		worker = workers->get_next_master(workers, &it);

		struct _starpu_fifo_taskq *fifo = dt->queue_array[worker];
		struct starpu_perfmodel_arch* perf_arch = starpu_worker_get_perf_archtype(worker, sched_ctx_id);
		unsigned memory_node = starpu_worker_get_memory_node(worker);

		STARPU_ASSERT_MSG(fifo != NULL, "worker %u ctx %u\n", worker, sched_ctx_id);

		/* Sometimes workers didn't take the tasks as early as we expected */
		double exp_start = isnan(fifo->exp_start) ? starpu_timing_now() : STARPU_MAX(fifo->exp_start, starpu_timing_now());
		if (!starpu_worker_can_execute_task_impl(worker, task, &impl_mask))
			continue;

		for (nimpl  = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
		{
			if (!(impl_mask & (1U << nimpl)))
			{
				/* no one on that queue may execute this task */
				continue;
			}

			int fifo_ntasks = fifo->ntasks;
			double prev_exp_len = fifo->exp_len;
			/* consider the priority of the task when deciding on which worker to schedule, 
			   compute the expected_end of the task if it is inserted before other tasks already scheduled */
			if(sorted_decision)
			{
				if(dt->num_priorities != -1)
				{
					prev_exp_len = fifo->exp_len_per_priority[task_prio];
					fifo_ntasks = fifo->ntasks_per_priority[task_prio];
				}
				else
				{
					starpu_pthread_mutex_t *sched_mutex;
					starpu_pthread_cond_t *sched_cond;
					starpu_worker_get_sched_condition(worker, &sched_mutex, &sched_cond);
					prev_exp_len = _starpu_fifo_get_exp_len_prev_task_list(fifo, task, worker, nimpl, &fifo_ntasks);
				}
			}
				
			exp_end[worker_ctx][nimpl] = exp_start + prev_exp_len;
			if (exp_end[worker_ctx][nimpl] > max_exp_end)
				max_exp_end = exp_end[worker_ctx][nimpl];

			//_STARPU_DEBUG("Scheduler dmda: task length (%lf) worker (%u) kernel (%u) \n", local_task_length[worker][nimpl],worker,nimpl);

			if (bundle)
			{
				/* TODO : conversion time */
				local_task_length[worker_ctx][nimpl] = starpu_task_bundle_expected_length(bundle, perf_arch, nimpl);
				local_data_penalty[worker_ctx][nimpl] = starpu_task_bundle_expected_data_transfer_time(bundle, memory_node);
				local_energy[worker_ctx][nimpl] = starpu_task_bundle_expected_energy(bundle, perf_arch,nimpl);

			}
			else
			{
				local_task_length[worker_ctx][nimpl] = starpu_task_expected_length(task, perf_arch, nimpl);
				local_data_penalty[worker_ctx][nimpl] = starpu_task_expected_data_transfer_time(memory_node, task);
				local_energy[worker_ctx][nimpl] = starpu_task_expected_energy(task, perf_arch,nimpl);
				double conversion_time = starpu_task_expected_conversion_time(task, perf_arch, nimpl);
				if (conversion_time > 0.0)
					local_task_length[worker_ctx][nimpl] += conversion_time;
			}
			double ntasks_end = fifo_ntasks / starpu_worker_get_relative_speedup(perf_arch);

			/*
			 * This implements a default greedy scheduler for the
			 * case of tasks which have no performance model, or
			 * whose performance model is not calibrated yet.
			 *
			 * It simply uses the number of tasks already pushed to
			 * the workers, divided by the relative performance of
			 * a CPU and of a GPU.
			 *
			 * This is always computed, but the ntasks_best
			 * selection is only really used if the task indeed has
			 * no performance model, or is not calibrated yet.
			 */
			if (ntasks_best == -1

			    /* Always compute the greedy decision, at least for
			     * the tasks with no performance model. */
			    || (!calibrating && ntasks_end < ntasks_best_end)

			    /* The performance model of this task is not
			     * calibrated on this worker, try to run it there
			     * to calibrate it there. */
			    || (!calibrating && isnan(local_task_length[worker_ctx][nimpl]))

			    /* the performance model of this task is not
			     * calibrated on this worker either, rather run it
			     * there if this one is low on scheduled tasks. */
			    || (calibrating && isnan(local_task_length[worker_ctx][nimpl]) && ntasks_end < ntasks_best_end)
				)
			{
				ntasks_best_end = ntasks_end;
				ntasks_best = worker;
				nimpl_best = nimpl;
			}

			if (isnan(local_task_length[worker_ctx][nimpl]))
				/* we are calibrating, we want to speed-up calibration time
				 * so we privilege non-calibrated tasks (but still
				 * greedily distribute them to avoid dumb schedules) */
				calibrating = 1;

			if (isnan(local_task_length[worker_ctx][nimpl])
					|| _STARPU_IS_ZERO(local_task_length[worker_ctx][nimpl]))
				/* there is no prediction available for that task
				 * with that arch (yet or at all), so switch to a greedy strategy */
				unknown = 1;

			if (unknown)
				continue;

			exp_end[worker_ctx][nimpl] = exp_start + prev_exp_len + local_task_length[worker_ctx][nimpl];

			if (exp_end[worker_ctx][nimpl] < best_exp_end)
			{
				/* a better solution was found */
				best_exp_end = exp_end[worker_ctx][nimpl];
				nimpl_best = nimpl;
			}

			if (isnan(local_energy[worker_ctx][nimpl]))
				local_energy[worker_ctx][nimpl] = 0.;

		}
		worker_ctx++;
	}

	*forced_worker = unknown?ntasks_best:-1;
	*forced_impl = unknown?nimpl_best:-1;

#ifdef STARPU_VERBOSE
	if (unknown)
	{
		dt->eager_task_cnt++;
	}
#endif

	*best_exp_endp = best_exp_end;
	*max_exp_endp = max_exp_end;
}


// static int push_task_on_device_queue (unsigned sched_ctx_id){
// 		/* find the queue */

// 	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
//     struct starpu_task *task = starpu_task_list_pop_front (&dt->main_list);
// 	if(task == NULL)    {return -10;}

// 	unsigned worker, worker_ctx = 0;
// 	int best = -1, best_in_ctx = -1;
// 	int selected_impl = 0;
// 	double model_best = 0.0;
// 	double transfer_model_best = 0.0;

// 	/* this flag is set if the corresponding worker is selected because
// 	   there is no performance prediction available yet */
// 	int forced_best = -1;
// 	int forced_impl = -1;

// 	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);
// 	unsigned nworkers_ctx = workers->nworkers;
// 	double local_task_length[nworkers_ctx][STARPU_MAXIMPLEMENTATIONS];
// 	double local_data_penalty[nworkers_ctx][STARPU_MAXIMPLEMENTATIONS];
// 	double local_energy[nworkers_ctx][STARPU_MAXIMPLEMENTATIONS];

// 	/* Expected end of this task on the workers */
// 	double exp_end[nworkers_ctx][STARPU_MAXIMPLEMENTATIONS];

// 	/* This is the minimum among the exp_end[] matrix */
// 	double best_exp_end;

// 	/* This is the maximum termination time of already-scheduled tasks over all workers */
// 	double max_exp_end = 0.0;

// 	double fitness[nworkers_ctx][STARPU_MAXIMPLEMENTATIONS];


// 	compute_all_performance_predictions(task,
// 					    nworkers_ctx,
// 					    local_task_length,
// 					    exp_end,
// 					    &max_exp_end,
// 					    &best_exp_end,
// 					    local_data_penalty,
// 					    local_energy,
// 					    &forced_best,
// 					    &forced_impl, sched_ctx_id, 0);
	
	
// 	double best_fitness = -1;

// 	unsigned nimpl;
// 	unsigned impl_mask;
// 	if (forced_best == -1)
// 	{
// 		struct starpu_sched_ctx_iterator it;

// 		workers->init_iterator(workers, &it);
// 		while(workers->has_next_master(workers, &it))
// 		{
// 			worker = workers->get_next_master(workers, &it);
// 			if (!starpu_worker_can_execute_task_impl(worker, task, &impl_mask))
// 				continue;
// 			for (nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
// 			{
// 				if (!(impl_mask & (1U << nimpl)))
// 				{
// 					/* no one on that queue may execute this task */
// 					continue;
// 				}
// 				fitness[worker_ctx][nimpl] = dt->alpha*(exp_end[worker_ctx][nimpl] - best_exp_end)
// 					+ dt->beta*(local_data_penalty[worker_ctx][nimpl])
// 					+ dt->_gamma*(local_energy[worker_ctx][nimpl]);

// 				if (exp_end[worker_ctx][nimpl] > max_exp_end)
// 				{
// 					/* This placement will make the computation
// 					 * longer, take into account the idle
// 					 * consumption of other cpus */
// 					fitness[worker_ctx][nimpl] += dt->_gamma * dt->idle_power * (exp_end[worker_ctx][nimpl] - max_exp_end) / 1000000.0;
// 				}

// 				if (best == -1 || fitness[worker_ctx][nimpl] < best_fitness)
// 				{
// 					/* we found a better solution */
// 					best_fitness = fitness[worker_ctx][nimpl];
// 					best = worker;
// 					best_in_ctx = worker_ctx;
// 					selected_impl = nimpl;

// 					//_STARPU_DEBUG("best fitness (worker %d) %e = alpha*(%e) + beta(%e) +gamma(%e)\n", worker, best_fitness, exp_end[worker][nimpl] - best_exp_end, local_data_penalty[worker][nimpl], local_energy[worker][nimpl]);

// 				}
// 			}
// 			worker_ctx++;
// 		}
// 	}
// 	STARPU_ASSERT(forced_best != -1 || best != -1);

// 	if (forced_best != -1)
// 	{
// 		/* there is no prediction available for that task
// 		 * with that arch we want to speed-up calibration time
// 		 * so we force this measurement */
// 		best = forced_best;
// 		selected_impl = forced_impl;
// 		model_best = 0.0;
// 		transfer_model_best = 0.0;
// 	}
// 	else if (task->bundle)
// 	{
// 		struct starpu_perfmodel_arch* perf_arch = starpu_worker_get_perf_archtype(best_in_ctx, sched_ctx_id);
// 		unsigned memory_node = starpu_worker_get_memory_node(best);
// 		model_best = starpu_task_expected_length(task, perf_arch, selected_impl);
// 		transfer_model_best = starpu_task_expected_data_transfer_time(memory_node, task);
// 	}
// 	else
// 	{
// 		model_best = local_task_length[best_in_ctx][selected_impl];
// 		transfer_model_best = local_data_penalty[best_in_ctx][selected_impl];
// 	}

// 	//_STARPU_DEBUG("Scheduler dmda: kernel (%u)\n", best_impl);
// 	starpu_task_set_implementation(task, selected_impl);

// 	starpu_sched_task_break(task);

// 	/* we should now have the best worker in variable "best" */
// 	return push_task_on_best_worker(task, best, model_best, transfer_model_best, 0, sched_ctx_id);
// }

static int push_task_on_device_queue(unsigned sched_ctx_id){
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
    struct starpu_task *task = starpu_task_list_pop_front (&dt->main_list);
    if(task == NULL)    {return -10;}
	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection (sched_ctx_id);
	unsigned nworkers = workers->nworkers;

	unsigned worker;
	unsigned worker_ctx = 0;
	unsigned impl_mask, nimpl;

	double local_task_length[nworkers][STARPU_MAXIMPLEMENTATIONS];
	double local_data_penalty[nworkers][STARPU_MAXIMPLEMENTATIONS];
	double local_energy[nworkers][STARPU_MAXIMPLEMENTATIONS];	// NOT USED
	double exp_end[nworkers][STARPU_MAXIMPLEMENTATIONS];

	double exp_finish_time[nworkers][STARPU_MAXIMPLEMENTATIONS];

	double eariest_finish_time = 99999999999999999;
	unsigned best_worker, best_worker_ctx, best_impl;

	/* check  */
	starpu_task_bundle_t bundle = task->bundle;

	double fitness[nworkers][STARPU_MAXIMPLEMENTATIONS];	// NOT USE

	struct starpu_sched_ctx_iterator it;
	workers->init_iterator(workers, &it);
	while(workers->has_next_master(workers, &it)){
		worker = workers->get_next_master(workers, &it);
		struct _starpu_fifo_taskq *fifo = dt->queue_array[worker];
		unsigned memory_node = starpu_worker_get_memory_node(worker);
		struct starpu_perfmodel_arch* perf_arch = starpu_worker_get_perf_archtype (worker, sched_ctx_id);

		double exp_start = isnan(fifo->exp_start) ? starpu_timing_now(): STARPU_MAX(fifo->exp_start, starpu_timing_now());

		double prev_exp_len = fifo->exp_len;

		if (!starpu_worker_can_execute_task_impl(worker, task, &impl_mask))
			continue;
		for (nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++){
			if(!(impl_mask) & (1U << nimpl))
				continue;

			if (bundle){
				local_task_length[worker_ctx][nimpl] = starpu_task_bundle_expected_length(bundle, perf_arch, nimpl);
				local_data_penalty[worker_ctx][nimpl] = starpu_task_bundle_expected_data_transfer_time(bundle, memory_node);
				local_energy[worker_ctx][nimpl] = starpu_task_bundle_expected_energy(bundle, perf_arch, nimpl);
			}else{
				local_task_length[worker_ctx][nimpl] = starpu_task_expected_length(task, perf_arch, nimpl);
				local_data_penalty[worker_ctx][nimpl] = starpu_task_expected_data_transfer_time (memory_node, task);
				local_energy[worker_ctx][nimpl] = starpu_task_expected_energy (task, perf_arch,nimpl);
				double conversion_time = starpu_task_expected_conversion_time (task, perf_arch, nimpl);
				if (conversion_time > 0.0)
					local_task_length[worker_ctx][nimpl] += conversion_time; 
			}

			exp_end[worker_ctx][nimpl] = exp_start + prev_exp_len;

			exp_finish_time[worker_ctx][nimpl] = exp_end[worker_ctx][nimpl] + local_data_penalty[worker_ctx][nimpl] + local_task_length[worker_ctx][nimpl];
			if (exp_finish_time[worker_ctx][nimpl] < eariest_finish_time){
				eariest_finish_time = exp_finish_time[worker_ctx][nimpl];
				best_worker = worker;
				best_worker_ctx = worker_ctx;
				best_impl = nimpl;
			}
		}
		worker_ctx += 1;
	}

	starpu_task_set_implementation (task, best_impl);
	starpu_sched_task_break (task);
	
	double transfer_model_best = local_data_penalty[best_worker_ctx][best_impl];
	double model_best = local_task_length[best_worker_ctx][best_impl];
    // printf ("%d %f %f\n", best_worker, model_best, transfer_model_best);

    return push_task_on_best_worker (task, best_worker, model_best, transfer_model_best, 0, sched_ctx_id);

}

static double compute_task_heter_ratio(unsigned sched_ctx_id, struct starpu_task* task){
	return 1;
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data (sched_ctx_id);
    struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection (sched_ctx_id);
	unsigned nworkers_ctx = workers->nworkers;
	double max_task_len = MAX_TASK_LEN_INIT;
	double min_task_len = MIN_TASK_LEN_INIT;
	double cur_task_len;
	unsigned impl_mask;
	unsigned nimpl;
	struct starpu_sched_ctx_iterator it;
	unsigned worker;
	workers->init_iterator(workers, &it);
	while (workers->has_next_master(workers, &it)){
		worker = workers->get_next_master (workers, &it);
		struct _starpu_fifo_taskq *fifo = dt->queue_array[worker];
		unsigned memory_node = starpu_worker_get_memory_node (worker);
		struct starpu_perfmodel_arch* perf_arch = starpu_worker_get_perf_archtype (worker, sched_ctx_id);

		if (!starpu_worker_can_execute_task_impl (worker, task, &impl_mask))
			continue;
		
		for (nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++){
			if (!impl_mask & (1U << nimpl))
				continue;
			cur_task_len = starpu_task_expected_length (task, perf_arch, nimpl);
			if (!isnan(cur_task_len)){
				if (cur_task_len < min_task_len)	min_task_len = cur_task_len;
				if (cur_task_len > max_task_len)	max_task_len = cur_task_len;
			}
		}
	}

	/* since the task_len could be 0, add it by 1 */
	/* if no task's run time could be predicted, return - */
	if (max_task_len != MAX_TASK_LEN_INIT)
		return (max_task_len+1) / (min_task_len+1);		/* can min_task_len be 0 ?*/
	else
		return -1;
}

static void* check_threshold(){
    printf("in\n");
    static int k = 0;
	k = task_count;

    struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data (_sched_ctx_id);
	while(task_count>0) {
        STARPU_PTHREAD_MUTEX_LOCK (&dt->policy_mutex);
        int count = compute_tasks_on_device_queues(_sched_ctx_id);
        if (count < THRESHOLD && task_count > 0){
            push_task_on_device_queue (_sched_ctx_id);
			if(k != task_count){
				k = task_count;
				// printf ("test thread %d %d\n", count, task_count);
			}
        }
        STARPU_PTHREAD_MUTEX_UNLOCK (&dt->policy_mutex);
    }
}

static int starpu_list_size(struct starpu_task_list* list){
    struct starpu_task* cur = starpu_task_list_front(list);
    int count = 0;
    while(cur != NULL){
        count ++;
        cur = cur->next;
    }
    return count;
}

static int _hr_push_task(struct starpu_task *task, unsigned prio, unsigned sched_ctx_id, unsigned simulate, unsigned sorted_decision){
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	STARPU_PTHREAD_MUTEX_LOCK (&dt->policy_mutex);
	double task_hr_value = compute_task_heter_ratio (sched_ctx_id, task);
	task->hete_ratio = task_hr_value;
	// printf("%f\n", task_hr_value);
	if (task_hr_value == -1){
		starpu_task_list_push_back (&dt->main_list,task);
		
		/* just push it to the end of the dispatch list */
	}else{
		/* insert it to the main list */
		/* necessary to use lock, in case pop and insert at the same time */
		struct starpu_task* cur = starpu_task_list_front (&dt->main_list);
        if (cur == NULL)
			starpu_task_list_push_front (&dt->main_list, task);
		else if (task->hete_ratio > cur->hete_ratio){
			starpu_task_list_push_front (&dt->main_list, task);
		}else{
			cur = cur->next;
			while (cur!= NULL){
				if (task->hete_ratio > cur->hete_ratio){
					task->next = cur->next;
					task->prev = cur->prev;
					cur->prev->next = task;
					cur->next->prev = task;
					break;
				}else
					cur = cur->next;
			}
			starpu_task_list_push_back (&dt->main_list, task);
		}
	}
    // printf("%d\n", starpu_list_size(&dt->main_list));
	STARPU_PTHREAD_MUTEX_UNLOCK (&dt->policy_mutex);
	/* After this, this task has been inserted on the dispatch list */
	// push_task_on_device_queue (sched_ctx_id);
}

static int hr_push_task(struct starpu_task *task)
{
	STARPU_ASSERT(task);
	return _hr_push_task(task, 0, task->sched_ctx, 0, 0);
}

static double dmda_simulate_push_task(struct starpu_task *task)
{
	STARPU_ASSERT(task);
	return _hr_push_task(task, 0, task->sched_ctx, 1, 0);
}

static void dmda_add_workers(unsigned sched_ctx_id, int *workerids, unsigned nworkers)
{
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	unsigned i;
	for (i = 0; i < nworkers; i++)
	{
		struct _starpu_fifo_taskq *q;
		int workerid = workerids[i];
		/* if the worker has alreadry belonged to this context
		   the queue and the synchronization variables have been already initialized */
		q = dt->queue_array[workerid];
		if(q == NULL)
		{
			q = dt->queue_array[workerid] = _starpu_create_fifo();
			/* These are only stats, they can be read with races */
			STARPU_HG_DISABLE_CHECKING(q->exp_start);
			STARPU_HG_DISABLE_CHECKING(q->exp_len);
			STARPU_HG_DISABLE_CHECKING(q->exp_end);
		}

		if(dt->num_priorities != -1)
		{
			_STARPU_MALLOC(q->exp_len_per_priority, dt->num_priorities*sizeof(double));
			_STARPU_MALLOC(q->ntasks_per_priority, dt->num_priorities*sizeof(unsigned));
			int j;
			for(j = 0; j < dt->num_priorities; j++)
			{
				q->exp_len_per_priority[j] = 0.0;
				q->ntasks_per_priority[j] = 0;
			}
		}
	}
}

static void dmda_remove_workers(unsigned sched_ctx_id, int *workerids, unsigned nworkers)
{
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	int workerid;
	unsigned i;
	for (i = 0; i < nworkers; i++)
	{
		workerid = workerids[i];
		if(dt->queue_array[workerid] != NULL)
		{
			if(dt->num_priorities != -1)
			{
				free(dt->queue_array[workerid]->exp_len_per_priority);
				free(dt->queue_array[workerid]->ntasks_per_priority);
			}

			_starpu_destroy_fifo(dt->queue_array[workerid]);
			dt->queue_array[workerid] = NULL;
		}
	}
}

static void initialize_dmda_policy(unsigned sched_ctx_id)
{
	pthread_t thr_checker;
	pthread_create(&thr_checker, NULL, check_threshold, NULL);

    _sched_ctx_id = sched_ctx_id;
	starpu_sched_ctx_create_worker_collection(sched_ctx_id, STARPU_WORKER_LIST);

	struct _starpu_dmda_data *dt;
	_STARPU_CALLOC(dt, 1, sizeof(struct _starpu_dmda_data));

	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void*)dt);

	_STARPU_MALLOC(dt->queue_array, STARPU_NMAXWORKERS*sizeof(struct _starpu_fifo_taskq*));

	int i;
	for(i = 0; i < STARPU_NMAXWORKERS; i++)
		dt->queue_array[i] = NULL;

	dt->alpha = starpu_get_env_float_default("STARPU_SCHED_ALPHA", _STARPU_SCHED_ALPHA_DEFAULT);
	dt->beta = starpu_get_env_float_default("STARPU_SCHED_BETA", _STARPU_SCHED_BETA_DEFAULT);
	dt->_gamma = starpu_get_env_float_default("STARPU_SCHED_GAMMA", _STARPU_SCHED_GAMMA_DEFAULT);
	dt->idle_power = starpu_get_env_float_default("STARPU_IDLE_POWER", 0.0);

    starpu_task_list_init (&dt->main_list);
    STARPU_PTHREAD_MUTEX_INIT (&dt->policy_mutex, NULL);

	if(starpu_sched_ctx_min_priority_is_set(sched_ctx_id) != 0 && starpu_sched_ctx_max_priority_is_set(sched_ctx_id) != 0)
		dt->num_priorities = starpu_sched_ctx_get_max_priority(sched_ctx_id) - starpu_sched_ctx_get_min_priority(sched_ctx_id) + 1;
	else 
		dt->num_priorities = -1;


#ifdef STARPU_USE_TOP
	/* FIXME: broken, needs to access context variable */
	starpu_top_register_parameter_float("DMDA_ALPHA", &alpha,
					    alpha_minimum, alpha_maximum, param_modified);
	starpu_top_register_parameter_float("DMDA_BETA", &beta,
					    beta_minimum, beta_maximum, param_modified);
	starpu_top_register_parameter_float("DMDA_GAMMA", &_gamma,
					    gamma_minimum, gamma_maximum, param_modified);
	starpu_top_register_parameter_float("DMDA_IDLE_POWER", &idle_power,
					    idle_power_minimum, idle_power_maximum, param_modified);
#endif /* !STARPU_USE_TOP */
}

static void initialize_dmda_sorted_policy(unsigned sched_ctx_id)
{
	initialize_dmda_policy(sched_ctx_id);

	/* The application may use any integer */
	if (starpu_sched_ctx_min_priority_is_set(sched_ctx_id) == 0)
		starpu_sched_ctx_set_min_priority(sched_ctx_id, INT_MIN);
	if (starpu_sched_ctx_max_priority_is_set(sched_ctx_id) == 0)
		starpu_sched_ctx_set_max_priority(sched_ctx_id, INT_MAX);
}

static void deinitialize_dmda_policy(unsigned sched_ctx_id)
{
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
#ifdef STARPU_VERBOSE
	{
	struct _starpu_sched_ctx *sched_ctx = _starpu_get_sched_ctx_struct(sched_ctx_id);
	long int modelled_task_cnt = dt->total_task_cnt - dt->eager_task_cnt;
	_STARPU_DEBUG("%s sched policy (sched_ctx %u): total_task_cnt %ld ready_task_cnt %ld (%.1f%%), modelled_task_cnt = %ld (%.1f%%)%s\n",
		sched_ctx->sched_policy?sched_ctx->sched_policy->policy_name:"<none>",
		sched_ctx_id,
		dt->total_task_cnt,
		dt->ready_task_cnt,
		(100.0f*dt->ready_task_cnt)/dt->total_task_cnt,
		modelled_task_cnt,
		(100.0f*modelled_task_cnt)/dt->total_task_cnt,
		modelled_task_cnt==0?" *** Check if performance models are enabled and converging on a per-codelet basis, or use an non-modeling scheduling policy. ***":"");
	}
#endif

    STARPU_ASSERT (starpu_task_list_empty(&dt->main_list));
	free(dt->queue_array);
	free(dt);
	starpu_sched_ctx_delete_worker_collection(sched_ctx_id);
}

/* dmda_pre_exec_hook is called right after the data transfer is done and right
 * before the computation to begin, it is useful to update more precisely the
 * value of the expected start, end, length, etc... */
static void dmda_pre_exec_hook(struct starpu_task *task)
{
	unsigned sched_ctx_id = task->sched_ctx;
	unsigned workerid = starpu_worker_get_id_check();
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	struct _starpu_fifo_taskq *fifo = dt->queue_array[workerid];
	double model = task->predicted;
	double transfer_model = task->predicted_transfer;

	starpu_pthread_mutex_t *sched_mutex;
	starpu_pthread_cond_t *sched_cond;
	starpu_worker_get_sched_condition(workerid, &sched_mutex, &sched_cond);

	/* Once the task is executing, we can update the predicted amount
	 * of work. */
	// STARPU_PTHREAD_MUTEX_LOCK_SCHED(sched_mutex);

	/* Take the opportunity to update start time */
	fifo->exp_start = STARPU_MAX(starpu_timing_now(), fifo->exp_start);
	fifo->exp_end = fifo->exp_start + fifo->exp_len;

	if(!isnan(transfer_model))
	{
		/* The transfer is over, get rid of it in the completion
		 * prediction */
		fifo->exp_len -= transfer_model;
		if(dt->num_priorities != -1)
		{
			int i;
			int task_prio = _normalize_prio(task->priority, dt->num_priorities, task->sched_ctx);
			for(i = 0; i <= task_prio; i++)
				fifo->exp_len_per_priority[i] -= transfer_model;
		}

	}

	if(!isnan(model))
	{
		/* We now start the computation, get rid of it in the completion
		 * prediction */
		fifo->exp_len -= model;
		fifo->exp_start += model;
		if(dt->num_priorities != -1)
		{
			int i;
			int task_prio = _normalize_prio(task->priority, dt->num_priorities, task->sched_ctx);
			for(i = 0; i <= task_prio; i++)
				fifo->exp_len_per_priority[i] -= model;
		}
	}

	fifo->exp_end = fifo->exp_start + fifo->exp_len;
	// STARPU_PTHREAD_MUTEX_UNLOCK_SCHED(sched_mutex);
}

static void dmda_push_task_notify(struct starpu_task *task, int workerid, int perf_workerid, unsigned sched_ctx_id)
{
	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	struct _starpu_fifo_taskq *fifo = dt->queue_array[workerid];
	/* Compute the expected penality */
	struct starpu_perfmodel_arch *perf_arch = starpu_worker_get_perf_archtype(perf_workerid, sched_ctx_id);
	unsigned memory_node = starpu_worker_get_memory_node(workerid);

	double predicted = starpu_task_expected_length(task, perf_arch,
						       starpu_task_get_implementation(task));

	double predicted_transfer = starpu_task_expected_data_transfer_time(memory_node, task);
	starpu_pthread_mutex_t *sched_mutex;
	starpu_pthread_cond_t *sched_cond;
	starpu_worker_get_sched_condition(workerid, &sched_mutex, &sched_cond);


	/* Update the predictions */
	// STARPU_PTHREAD_MUTEX_LOCK_SCHED(sched_mutex);
	/* Sometimes workers didn't take the tasks as early as we expected */
	fifo->exp_start = isnan(fifo->exp_start) ? starpu_timing_now() : STARPU_MAX(fifo->exp_start, starpu_timing_now());
	fifo->exp_end = fifo->exp_start + fifo->exp_len;

	/* If there is no prediction available, we consider the task has a null length */
	if (!isnan(predicted_transfer))
	{
		if (starpu_timing_now() + predicted_transfer < fifo->exp_end)
		{
			/* We may hope that the transfer will be finished by
			 * the start of the task. */
			predicted_transfer = 0;
		}
		else
		{
			/* The transfer will not be finished by then, take the
			 * remainder into account */
			predicted_transfer = (starpu_timing_now() + predicted_transfer) - fifo->exp_end;
		}
		task->predicted_transfer = predicted_transfer;
		fifo->exp_end += predicted_transfer;
		fifo->exp_len += predicted_transfer;
		if(dt->num_priorities != -1)
		{
			int i;
			int task_prio = _normalize_prio(task->priority, dt->num_priorities, task->sched_ctx);
			for(i = 0; i <= task_prio; i++)
				fifo->exp_len_per_priority[i] += predicted_transfer;
		}

	}

	/* If there is no prediction available, we consider the task has a null length */
	if (!isnan(predicted))
	{
		task->predicted = predicted;
		fifo->exp_end += predicted;
		fifo->exp_len += predicted;
		if(dt->num_priorities != -1)
		{
			int i;
			int task_prio = _normalize_prio(task->priority, dt->num_priorities, task->sched_ctx);
			for(i = 0; i <= task_prio; i++)
				fifo->exp_len_per_priority[i] += predicted;
		}

	}
	if(dt->num_priorities != -1)
	{
		int i;
		int task_prio = _normalize_prio(task->priority, dt->num_priorities, task->sched_ctx);
		for(i = 0; i <= task_prio; i++)
			fifo->ntasks_per_priority[i]++;
	}

	fifo->ntasks++;

	// STARPU_PTHREAD_MUTEX_UNLOCK_SCHED(sched_mutex);
}

static void dmda_post_exec_hook(struct starpu_task * task)
{

	struct _starpu_dmda_data *dt = (struct _starpu_dmda_data*)starpu_sched_ctx_get_policy_data(task->sched_ctx);
	unsigned workerid = starpu_worker_get_id_check();
	struct _starpu_fifo_taskq *fifo = dt->queue_array[workerid];
	starpu_pthread_mutex_t *sched_mutex;
	starpu_pthread_cond_t *sched_cond;
	starpu_worker_get_sched_condition(workerid, &sched_mutex, &sched_cond);
	// STARPU_PTHREAD_MUTEX_LOCK_SCHED(sched_mutex);
	fifo->exp_start = starpu_timing_now();
	fifo->exp_end = fifo->exp_start + fifo->exp_len;
	// STARPU_PTHREAD_MUTEX_UNLOCK_SCHED(sched_mutex);
}

/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2011-2014                                Inria
 * Copyright (C) 2013                                     Simon Archipoff
 * Copyright (C) 2008-2016                                Université de Bordeaux
 * Copyright (C) 2010-2013,2015,2017                      CNRS
 *
 * StarPU is free software; you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation; either version 2.1 of the License, or (at
 * your option) any later version.
 *
 * StarPU is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * See the GNU Lesser General Public License in COPYING.LGPL for more details.
 */

/*
 *	This is just the trivial policy where every worker use the same
 *	JOB QUEUE.
 */


#ifdef STARPU_QUICK_CHECK
#define NTASKS	320
#elif !defined(STARPU_LONG_CHECK)
#define NTASKS	3200
#else
#define NTASKS	32000
#endif




/*---------------------------------hr-scheduler---------------------------------*/

struct _starpu_hr_data
{
	double alpha;
	double beta;
	double _gamma;
	double idle_power;
    
    struct starpu_task_list main_list;
	starpu_pthread_mutex_t mainlist_mutex;
    
    struct _starpu_fifo_taskq **queue_array;

	long int total_task_cnt;
	long int ready_task_cnt;
	long int eager_task_cnt; /* number of tasks scheduled without model */
	int num_priorities;
};

static void hr_add_workers(unsigned sched_ctx_id, int *workerids, unsigned nworkers)
{
	struct _starpu_hr_data *dt = (struct _starpu_hr_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	
	unsigned i;
	for (i = 0; i < nworkers; i++)
	{
		struct _starpu_fifo_taskq *q;
		int workerid = workerids[i];
		/* if the worker has alreadry belonged to this context
		   the queue and the synchronization variables have been already initialized */
		q = dt->queue_array[workerid];
		if(q == NULL)
		{
			q = dt->queue_array[workerid] = _starpu_create_fifo();
			/* These are only stats, they can be read with races */
			STARPU_HG_DISABLE_CHECKING(q->exp_start);
			STARPU_HG_DISABLE_CHECKING(q->exp_len);
			STARPU_HG_DISABLE_CHECKING(q->exp_end);
		}

		if(dt->num_priorities != -1)
		{
			_STARPU_MALLOC(q->exp_len_per_priority, dt->num_priorities*sizeof(double));
			_STARPU_MALLOC(q->ntasks_per_priority, dt->num_priorities*sizeof(unsigned));
			int j;
			for(j = 0; j < dt->num_priorities; j++)
			{
				q->exp_len_per_priority[j] = 0.0;
				q->ntasks_per_priority[j] = 0;
			}
		}
	}
}

static void hr_remove_workers(unsigned sched_ctx_id, int *workerids, unsigned nworkers)
{
	struct _starpu_hr_data *dt = (struct _starpu_hr_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	int workerid;
	unsigned i;
	for (i = 0; i < nworkers; i++)
	{
		workerid = workerids[i];
		if(dt->queue_array[workerid] != NULL)
		{
			if(dt->num_priorities != -1)
			{
				free(dt->queue_array[workerid]->exp_len_per_priority);
				free(dt->queue_array[workerid]->ntasks_per_priority);
			}

			_starpu_destroy_fifo(dt->queue_array[workerid]);
			dt->queue_array[workerid] = NULL;
		}
	}
}

static void initialize_hr_policy(unsigned sched_ctx_id)
{	
	pthread_t thr_checker;
	pthread_create(&thr_checker, NULL, check_threshold, NULL);

	_sched_ctx_id = sched_ctx_id;

	starpu_sched_ctx_create_worker_collection(sched_ctx_id, STARPU_WORKER_LIST);

	struct _starpu_hr_data *dt;
	_STARPU_CALLOC(dt, 1, sizeof(struct _starpu_hr_data));

	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void*)dt);

	_STARPU_MALLOC(dt->queue_array, STARPU_NMAXWORKERS*sizeof(struct _starpu_fifo_taskq*));

	int i;
	for(i = 0; i < STARPU_NMAXWORKERS; i++)
		dt->queue_array[i] = NULL;

	dt->alpha = starpu_get_env_float_default("STARPU_SCHED_ALPHA", _STARPU_SCHED_ALPHA_DEFAULT);
	dt->beta = starpu_get_env_float_default("STARPU_SCHED_BETA", _STARPU_SCHED_BETA_DEFAULT);
	dt->_gamma = starpu_get_env_float_default("STARPU_SCHED_GAMMA", _STARPU_SCHED_GAMMA_DEFAULT);
	dt->idle_power = starpu_get_env_float_default("STARPU_IDLE_POWER", 0.0);

    starpu_task_list_init (&dt->main_list);
    STARPU_PTHREAD_MUTEX_INIT (&dt->mainlist_mutex, NULL);

	if(starpu_sched_ctx_min_priority_is_set(sched_ctx_id) != 0 && starpu_sched_ctx_max_priority_is_set(sched_ctx_id) != 0)
		dt->num_priorities = starpu_sched_ctx_get_max_priority(sched_ctx_id) - starpu_sched_ctx_get_min_priority(sched_ctx_id) + 1;
	else 
		dt->num_priorities = -1;


#ifdef STARPU_USE_TOP
	/* FIXME: broken, needs to access context variable */
	starpu_top_register_parameter_float("DMDA_ALPHA", &alpha,
					    alpha_minimum, alpha_maximum, param_modified);
	starpu_top_register_parameter_float("DMDA_BETA", &beta,
					    beta_minimum, beta_maximum, param_modified);
	starpu_top_register_parameter_float("DMDA_GAMMA", &_gamma,
					    gamma_minimum, gamma_maximum, param_modified);
	starpu_top_register_parameter_float("DMDA_IDLE_POWER", &idle_power,
					    idle_power_minimum, idle_power_maximum, param_modified);
#endif /* !STARPU_USE_TOP */
}


static void deinitialize_hr_policy(unsigned sched_ctx_id)
{
	struct _starpu_hr_data *dt = (struct _starpu_hr_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
#ifdef STARPU_VERBOSE
	{
	struct _starpu_sched_ctx *sched_ctx = _starpu_get_sched_ctx_struct(sched_ctx_id);
	long int modelled_task_cnt = dt->total_task_cnt - dt->eager_task_cnt;
	_STARPU_DEBUG("%s sched policy (sched_ctx %u): total_task_cnt %ld ready_task_cnt %ld (%.1f%%), modelled_task_cnt = %ld (%.1f%%)%s\n",
		sched_ctx->sched_policy?sched_ctx->sched_policy->policy_name:"<none>",
		sched_ctx_id,
		dt->total_task_cnt,
		dt->ready_task_cnt,
		(100.0f*dt->ready_task_cnt)/dt->total_task_cnt,
		modelled_task_cnt,
		(100.0f*modelled_task_cnt)/dt->total_task_cnt,
		modelled_task_cnt==0?" *** Check if performance models are enabled and converging on a per-codelet basis, or use an non-modeling scheduling policy. ***":"");
	}
#endif
    STARPU_ASSERT (starpu_task_list_empty(&dt->main_list));
	free(dt->queue_array);
	free(dt);
	starpu_sched_ctx_delete_worker_collection(sched_ctx_id);
}

struct starpu_sched_policy _starpu_sched_hr_policy = 
{
	.init_sched = initialize_hr_policy,
	.deinit_sched = deinitialize_hr_policy,
	.add_workers = dmda_add_workers ,
	.remove_workers = dmda_remove_workers,
	.push_task = hr_push_task,
	.pop_task = dmda_pop_task,
	.policy_name = "hr",
	.policy_description = "heterogeneity ratio performance model"
};





static int k = 0;
void dummy_func(void *descr[] STARPU_ATTRIBUTE_UNUSED, void *arg STARPU_ATTRIBUTE_UNUSED)
{	
    int m = 0;
    for(int i = 0; i < 1000000; i++)
        m += i;
	k = k+1;
	printf("finish %d\n",k);
    usleep(10);
}

static struct starpu_codelet dummy_codelet =
{
	.cpu_funcs = {dummy_func},
	.cpu_funcs_name = {"dummy_func"},
	.cuda_funcs = {dummy_func},
    .opencl_funcs = {dummy_func},
	.model = NULL,
	.nbuffers = 0,
	.name = "dummy",
};


















#ifdef STARPU_USE_CUDA
void cuda_kernel(void **descr, void *cl_arg);
#endif





/* default value */
static unsigned ntasks = 1024;

static unsigned long long nshot_per_task = 16*1024*1024ULL;

void cpu_kernel(void *descr[], void *cl_arg)
{
	unsigned *directions = (unsigned *)STARPU_VECTOR_GET_PTR(descr[0]);
	unsigned nx = nshot_per_task;

	TYPE *random_numbers = malloc(2*nx*sizeof(TYPE));
	sobolCPU(2*nx/n_dimensions, n_dimensions, directions, random_numbers);

	TYPE *random_numbers_x = &random_numbers[0];
	TYPE *random_numbers_y = &random_numbers[nx];

	unsigned current_cnt = 0;

	unsigned i;
	for (i = 0; i < nx; i++)
	{
		TYPE x = random_numbers_x[i];
		TYPE y = random_numbers_y[i];

		TYPE dist = (x*x + y*y);

		unsigned success = (dist <= 1.0);
		current_cnt += success;
	}

	unsigned *cnt = (unsigned *)STARPU_VECTOR_GET_PTR(descr[1]);
	*cnt = current_cnt;
	//printf("%d\n", current_cnt);

	free(random_numbers);
}

/* The amount of work does not depend on the data size at all :) */
static size_t size_base(struct starpu_task *task, unsigned nimpl)
{
	return nshot_per_task;
}

static void parse_args(int argc, char **argv)
{
	int i;
	for (i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "-ntasks") == 0)
		{
			char *argptr;
			ntasks = strtol(argv[++i], &argptr, 10);
			task_count = ntasks;
		}

		if (strcmp(argv[i], "-nshot") == 0)
		{
			char *argptr;
			nshot_per_task = strtol(argv[++i], &argptr, 10);
		}
		if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0)
		{
			fprintf(stderr,"Usage: %s [options...]\n", argv[0]);
			fprintf(stderr,"\n");
			fprintf(stderr,"Options:\n");
			fprintf(stderr,"-ntasks <n>		select the number of tasks\n");
			fprintf(stderr,"-nshot <n>		select the number of shot per task\n");
			exit(0);
		}
	}
}

static struct starpu_perfmodel model =
{
	.type = STARPU_HISTORY_BASED,
	.size_base = size_base,
	.symbol = "monte_carlo_pi"
};

static struct starpu_codelet pi_cl =
{
	.cpu_funcs = {cpu_kernel},
	.cpu_funcs_name = {"cpu_kernel"},

#ifdef STARPU_USE_CUDA
	.cuda_funcs = {cuda_kernel},
#endif

	.nbuffers = 2,
	.modes = {STARPU_R, STARPU_W},
	.model = &model
};

int main(int argc, char **argv)
{
	unsigned i;
	int ret;
	struct starpu_conf conf;
	parse_args(argc, argv);

#ifdef STARPU_HAVE_UNSETENV
	unsetenv("STARPU_SCHED");
#endif

	starpu_conf_init(&conf);
	conf.sched_policy = &_starpu_sched_hr_policy;
	ret = starpu_init(&conf);
	// ret = starpu_init(NULL);

	
	// pthread_t thr_checker;
	// pthread_create(&thr_checker, NULL, check_threshold, NULL);

	if (ret == -ENODEV)
		return 77;
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");
	/* Initialize the random number generator */
	unsigned *sobol_qrng_directions = malloc(n_dimensions*n_directions*sizeof(unsigned));
	STARPU_ASSERT(sobol_qrng_directions);

	initSobolDirectionVectors(n_dimensions, sobol_qrng_directions);

	/* Any worker may use that array now */
	starpu_data_handle_t sobol_qrng_direction_handle;
	starpu_vector_data_register(&sobol_qrng_direction_handle, STARPU_MAIN_RAM,
		(uintptr_t)sobol_qrng_directions, n_dimensions*n_directions, sizeof(unsigned));

	unsigned *cnt_array = calloc(ntasks, sizeof(unsigned));
	STARPU_ASSERT(cnt_array);
	starpu_data_handle_t cnt_array_handle;
	starpu_vector_data_register(&cnt_array_handle, STARPU_MAIN_RAM, (uintptr_t)cnt_array, ntasks, sizeof(unsigned));

	/* Use a write-through policy : when the data is modified on an
	 * accelerator, we know that it will only be modified once and be
	 * accessed by the CPU later on */
	starpu_data_set_wt_mask(cnt_array_handle, (1<<0));

	struct starpu_data_filter f =
	{
		.filter_func = starpu_vector_filter_block,
		.nchildren = ntasks
	};

	starpu_data_partition(cnt_array_handle, &f);

	double start;
	double end;

	start = starpu_timing_now();

	task_count = ntasks;
	for (i = 0; i < ntasks; i++)
	{
		struct starpu_task *task = starpu_task_create();

		task->cl = &pi_cl;

		STARPU_ASSERT(starpu_data_get_sub_data(cnt_array_handle, 1, i));

		task->handles[0] = sobol_qrng_direction_handle;
		task->handles[1] = starpu_data_get_sub_data(cnt_array_handle, 1, i);
		// printf("%d\n", i);
		ret = starpu_task_submit(task);
		STARPU_ASSERT(!ret);
	}

	starpu_task_wait_for_all();

	/* Get the cnt_array back in main memory */
	starpu_data_unpartition(cnt_array_handle, STARPU_MAIN_RAM);
	starpu_data_unregister(cnt_array_handle);
	starpu_data_unregister(sobol_qrng_direction_handle);

	/* Count the total number of entries */
	unsigned long total_cnt = 0;
	for (i = 0; i < ntasks; i++)
		total_cnt += cnt_array[i];

	end = starpu_timing_now();

	double timing = end - start;

	unsigned long total_shot_cnt = ntasks * nshot_per_task;

	/* Total surface : Pi * r^ 2 = Pi*1^2, total square surface : 2^2 = 4, probability to impact the disk: pi/4 */
	FPRINTF(stderr, "Pi approximation : %f (%lu / %lu)\n", ((TYPE)total_cnt*4)/(total_shot_cnt), total_cnt, total_shot_cnt);
	FPRINTF(stderr, "Total time : %f ms\n", timing/1000.0);
	FPRINTF(stderr, "Speed : %f GShot/s\n", total_shot_cnt/(1e3*timing));

	if (!getenv("STARPU_SSILENT")) starpu_codelet_display_stats(&pi_cl);

	starpu_shutdown();

	return 0;
}


// int main(int argc, char **argv)
// {
// 	int ntasks = NTASKS;
// 	int ret;
// 	struct starpu_conf conf;

// #ifdef STARPU_HAVE_UNSETENV
// 	unsetenv("STARPU_SpuCHED");
// #endif

// 	starpu_conf_init(&conf);
// 	conf.sched_policy = &_starpu_sched_hr_policy,
// 	ret = starpu_init(&conf);
// 	if (ret == -ENODEV)
// 		return 77;
// 	STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");

//     pthread_t thr_checker;
//     pthread_create(&thr_checker, NULL, check_threshold, NULL);


// #ifdef STARPU_QUICK_CHECK
// 	ntasks /= 100;
// #endif
// 	ntasks = 100;

//     task_count = ntasks;
//     STARPU_PTHREAD_MUTEX_INIT (&count_lock, NULL);
// 	int i;
// 	for (i = 0; i < ntasks; i++)
// 	{
// 		struct starpu_task *task = starpu_task_create();

// 		task->cl = &dummy_codelet;
// 		task->cl_arg = NULL;
// 		ret = starpu_task_submit(task);
// 		STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");
// 	}

// 	starpu_task_wait_for_all();

//     if (!getenv("STARPU_SSILENT")) starpu_codelet_display_stats(&dummy_codelet);

// 	starpu_shutdown();

// 	return 0;
// }
