/* StarPU --- Runtime system for heterogeneous multicore architectures.
 *
 * Copyright (C) 2012-2013                                Inria
 * Copyright (C) 2010-2017                                Université de Bordeaux
 * Copyright (C) 2010-2013,2015-2017                      CNRS
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
 * This is an example of an application-defined scheduler.
 * This is a mere eager scheduler with a centralized list of tasks to schedule:
 * when a task becomes ready (push) it is put on the list. When a device
 * becomes ready (pop), a task is taken from the list.
 */
#include <starpu.h>
#include <starpu_scheduler.h>
#include <stdlib.h>
#include <starpu_task.h>

#ifdef STARPU_QUICK_CHECK
#define NTASKS 320
#elif !defined(STARPU_LONG_CHECK)
#define NTASKS 3200
#else
#define NTASKS 32000
#endif
#define FPRINTF(ofile, fmt, ...)                \
	do                                          \
	{                                           \
		if (!getenv("STARPU_SSILENT"))          \
		{                                       \
			fprintf(ofile, fmt, ##__VA_ARGS__); \
		}                                       \
	} while (0)
#define thr 10 //predeﬁned threshold

static double get_task_heter_ratio(unsigned sched_ctx_id, struct starpu_task *task);
static double get_rank(unsigned sched_ctx_id, struct starpu_task *task);

int all_device_len = 0; // the total number of assigned tasks in all device queues

struct dummy_sched_data
{
	struct starpu_task_list sched_list;
	starpu_pthread_mutex_t policy_mutex;
	struct starpu_task_list *worker_sched_list;
};

static void init_dummy_sched(unsigned sched_ctx_id)
{
	starpu_sched_ctx_create_worker_collection(sched_ctx_id, STARPU_WORKER_LIST);

	struct dummy_sched_data *data = (struct dummy_sched_data *)malloc(sizeof(struct dummy_sched_data));

	unsigned int worker_num = starpu_worker_get_count();

	data->worker_sched_list = (struct starpu_task_list *)malloc((worker_num) * sizeof(struct starpu_task_list));
	/* Create a linked-list of tasks and a condition variable to protect it */
	starpu_task_list_init(&data->sched_list);
	for (unsigned int i = 0; i < worker_num; i++)
	{
		starpu_task_list_init(&data->worker_sched_list[i]);
	}

	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void *)data);

	STARPU_PTHREAD_MUTEX_INIT(&data->policy_mutex, NULL);
	FPRINTF(stderr, "Initialising Dummy scheduler\n");
}

static void deinit_dummy_sched(unsigned sched_ctx_id)
{
	struct dummy_sched_data *data = (struct dummy_sched_data *)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	STARPU_ASSERT(starpu_task_list_empty(&data->sched_list));
	unsigned int worker_num = starpu_worker_get_count();
	for (unsigned int i = 0; i < worker_num; i++)
	{
		STARPU_ASSERT(starpu_task_list_empty(&data->worker_sched_list[i]));
	}
	starpu_sched_ctx_delete_worker_collection(sched_ctx_id);

	STARPU_PTHREAD_MUTEX_DESTROY(&data->policy_mutex);

	free(data);

	FPRINTF(stderr, "Destroying Dummy scheduler\n");
}

static int push_task_on_device(unsigned sched_ctx_id)
{
	struct dummy_sched_data *data = (struct dummy_sched_data *)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);
	struct starpu_task *task = starpu_task_list_pop_front(&data->sched_list);
	//fprintf(stdout, "Hello");
	struct starpu_sched_ctx_iterator it;
	//workers->init_iterator(workers, &it);
	unsigned worker;
	int ran = rand() % 3;
	starpu_task_list_push_back(&data->worker_sched_list[ran], task); //TODO
	return 0;
}

static void starpu_task_list_swap(struct starpu_task_list *list, struct starpu_task *a, struct starpu_task *b)
{
	struct starpu_task *pre = a->prev;
	struct starpu_task *nex = b->next;
	if (a == list->head)
		list->head = b;
	if (b == list->tail)
		list->tail = a;
	if (pre != NULL)
	{
		pre->next = b;
		b->prev = pre;
	}
	b->next = a;
	if (nex != NULL)
	{
		a->next = nex;
		nex->prev = a;
	}
	a->prev = b;
}

static void starpu_task_list_sort_on_heter_ratio(struct starpu_task_list *list, unsigned sched_ctx_id)
{
	int going = 1;
	while (going)
	{
		going = 0;
		struct starpu_task *current = list->head;
		while (current != list->tail)
		{
			if (get_task_heter_ratio(sched_ctx_id, current) < get_task_heter_ratio(sched_ctx_id, current->next))
			{
				going = 1;
				starpu_task_list_swap(list, current, current->next);
			}
			else
			{
				current = current->next;
			}
		}
	}
}

static int push_task_dummy(struct starpu_task *task)
{
	unsigned sched_ctx_id = task->sched_ctx;
	struct dummy_sched_data *data = (struct dummy_sched_data *)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	/* NB: In this simplistic strategy, we assume that the context in which
	   we push task has at least one worker*/

	starpu_task_list_sort_on_heter_ratio(&data->sched_list, sched_ctx_id);
	/* lock all workers when pushing tasks on a list where all
	   of them would pop for tasks */
	double rank;
	rank = get_rank(sched_ctx_id, task);
	printf("the rank is %d\n", rank);
	STARPU_PTHREAD_MUTEX_LOCK(&data->policy_mutex);
	double max_heter_ratio = get_task_heter_ratio(sched_ctx_id, task);
	// printf("%lf\n", max_heter_ratio);
	// starpu_task_list_push_back(&data->sched_list, task);
	if (starpu_task_list_empty(&data->sched_list))
	{
		starpu_task_list_push_back(&data->sched_list, task);
	}
	else
	{
		struct starpu_task *current = starpu_task_list_begin(&data->sched_list);
		if (max_heter_ratio > get_task_heter_ratio(sched_ctx_id, current))
		{
			starpu_task_list_push_front(&data->sched_list, task);
		}
		else
		{
			current = current->next;
			while (current != NULL)
			{
				if (max_heter_ratio > get_task_heter_ratio(sched_ctx_id, current))
				{
					current->prev->next = task;
					task->next = current;
					task->prev = current->prev;
					current->prev = task;
				}
			}
		}
	}
	if (all_device_len < thr)
		push_task_on_device(sched_ctx_id);
	starpu_push_task_end(task);
	STARPU_PTHREAD_MUTEX_UNLOCK(&data->policy_mutex);

	/*if there are no tasks block */
	/* wake people waiting for a task */
	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);

	struct starpu_sched_ctx_iterator it;

	// workers->init_iterator(workers, &it);
	// while(workers->has_next(workers, &it))
	//     {
	// 	unsigned worker;
	//             worker = workers->get_next(workers, &it);
	// 	starpu_pthread_mutex_t *sched_mutex;
	//             starpu_pthread_cond_t *sched_cond;
	//             starpu_worker_get_sched_condition(worker, &sched_mutex, &sched_cond);
	// 	STARPU_PTHREAD_MUTEX_LOCK(sched_mutex);
	//             STARPU_PTHREAD_COND_SIGNAL(sched_cond);
	//             STARPU_PTHREAD_MUTEX_UNLOCK(sched_mutex);
	//     }

	return 0;
}
static double get_task_heter_ratio(unsigned sched_ctx_id, struct starpu_task *task)
{
	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);

	struct starpu_sched_ctx_iterator it;
	struct starpu_sched_ctx_iterator it1;
	double max_execution_time = 0;
	double max_heter_tatio = 0;
	unsigned worker;
	unsigned impl_mask;
	unsigned nimpl;

	workers->init_iterator(workers, &it);
	while (workers->has_next_master(workers, &it))
	{
		worker = workers->get_next_master(workers, &it);
		unsigned memory_node = starpu_worker_get_memory_node(worker);
		struct starpu_perfmodel_arch *perf_arch = starpu_worker_get_perf_archtype(worker, sched_ctx_id);
		if (!starpu_worker_can_execute_task_impl(worker, task, &impl_mask))
			continue;

		for (nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
		{
			if (!(impl_mask & (1U << nimpl)))
			{
				/* no one on that queue may execute this task */
				continue;
			}
			double local_length = 1 + starpu_task_expected_length(task, perf_arch, nimpl);
			printf("expected length is %lf\n", local_length);
			if (local_length > max_execution_time)
				max_execution_time = local_length;
		}
	}
	printf("the max_execution_time is %lf\n", max_execution_time);

	workers->init_iterator(workers, &it1);
	while (workers->has_next_master(workers, &it1))
	{
		worker = workers->get_next_master(workers, &it1);
		unsigned memory_node = starpu_worker_get_memory_node(worker);
		struct starpu_perfmodel_arch *perf_arch = starpu_worker_get_perf_archtype(worker, sched_ctx_id);
		if (!starpu_worker_can_execute_task_impl(worker, task, &impl_mask))
			continue;

		for (nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
		{
			if (!(impl_mask & (1U << nimpl)))
			{
				/* no one on that queue may execute this task */
				continue;
			}
			double local_length = 1 + starpu_task_expected_length(task, perf_arch, nimpl);
			double heter_ratio = max_execution_time / local_length;
			if (heter_ratio > max_heter_tatio)
				max_heter_tatio = heter_ratio;
		}
	}
	return max_heter_tatio;
}

static double avg_execution_time(unsigned sched_ctx_id, struct starpu_task *task)
{
	unsigned m = starpu_worker_get_count();
	double ances_completion_time = 0;
	unsigned impl_mask;
	unsigned nimpl;

	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);
	struct starpu_sched_ctx_iterator it;

	//compute the first part in rank
	workers->init_iterator(workers, &it);
	while (workers->has_next_master(workers, &it))
	{
		unsigned worker = workers->get_next_master(workers, &it);
		// unsigned memory_node = starpu_worker_get_memory_node(worker);
		struct starpu_perfmodel_arch *perf_arch = starpu_worker_get_perf_archtype(worker, sched_ctx_id);
		if (!starpu_worker_can_execute_task_impl(worker, task, &impl_mask))
			continue;

		for (nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
		{
			if (!(impl_mask & (1U << nimpl)))
			{
				/* no worker can execute the impl */
				continue;
			}
			double local_length = starpu_task_expected_length(task, perf_arch, nimpl);
			ances_completion_time += local_length;
		}
	}
	return ances_completion_time / m;
}
/*Based on /home/undergrats/XY/starPU/starpu_bk/src/core/perfmodel/perfmodel.c*/
static double gengral_data_transfer_time(struct starpu_task *ances, struct starpu_task *sucess) //T() transfer a to b
{
	unsigned nbuffers = STARPU_TASK_GET_NBUFFERS(sucess);
	unsigned buffer = 0;
	double penalty = 0.0;
	unsigned src_node = 0;
	unsigned des_node = 0;

	unsigned m = starpu_worker_get_count();

	for (buffer = 0; buffer < nbuffers; buffer++)
	{
		starpu_data_handle_t handle = STARPU_TASK_GET_HANDLE(sucess, buffer);
		size_t size = _starpu_data_get_size(handle);
		for (int i = 0; i < m; i++)
		{
			for (int j = 0; j < m; j++)
			{
				src_node = starpu_worker_get_memory_node(i);
				des_node = starpu_worker_get_memory_node(j);
				if (i == j)
					continue;
				penalty += starpu_transfer_predict(src_node, des_node, size);
			}
		}
	}
	return penalty / (m * m);
}

static double get_rank(unsigned sched_ctx_id, struct starpu_task *task)
{
	double ances_exe_time = avg_execution_time(sched_ctx_id, task);
	double max_trans_plus_exe = 0;
	int succe_size = statpu_task_get_task_succs(task, 0, NULL);
	struct starpu_task *succe[succe_size];
	starpu_task_get_task_succs(task, sizeof(succe) / sizeof(*succe), succe);
	for (int i = 0; i < succe_size; i++)
	{
		double succe_exe_time = avg_execution_time(sched_ctx_id, succe[i]);
		double trans_time = gengral_data_transfer_time(task, succe[i]);
		double trans_plus_exe = succe_exe_time + trans_time;
		if (trans_plus_exe > max_trans_plus_exe)
		{
			max_trans_plus_exe = trans_plus_exe;
		}
	}
	return ances_exe_time + max_trans_plus_exe;
}

/* The mutex associated to the calling worker is already taken by StarPU */
static struct starpu_task *pop_task_dummy(unsigned sched_ctx_id)
{
	/* NB: In this simplistic strategy, we assume that all workers are able
	 * to execute all tasks, otherwise, it would have been necessary to go
	 * through the entire list until we find a task that is executable from
	 * the calling worker. So we just take the head of the list and give it
	 * to the worker. */
	//fprintf(stdout, "pop");
	struct dummy_sched_data *data = (struct dummy_sched_data *)starpu_sched_ctx_get_policy_data(sched_ctx_id);
	// #ifdef STARPU_NON_BLOCKING_DRIVERS
	// 	if (starpu_task_list_empty(&data->sched_list))
	// 		return NULL;
	// #endif
	STARPU_PTHREAD_MUTEX_LOCK(&data->policy_mutex);
	unsigned workerid = starpu_worker_get_id_check();
	struct starpu_task *task = NULL;
	if (!starpu_task_list_empty(&data->worker_sched_list[workerid]))
		task = starpu_task_list_pop_front(&data->worker_sched_list[workerid]);
	STARPU_PTHREAD_MUTEX_UNLOCK(&data->policy_mutex);
	return task;
}

static struct starpu_sched_policy dummy_sched_policy =
	{
		.init_sched = init_dummy_sched,
		.deinit_sched = deinit_dummy_sched,
		.push_task = push_task_dummy,
		.pop_task = pop_task_dummy,
		.policy_name = "dummy",
		.policy_description = "dummy scheduling strategy"};

void dummy_func(void *descr[] STARPU_ATTRIBUTE_UNUSED, void *arg STARPU_ATTRIBUTE_UNUSED)
{
	int k = 0;
	for (int i = 0; i < 1000000000; i++)
	{
		k = i * 20;
	}
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

int main(int argc, char **argv)
{
	int ntasks = NTASKS;
	int ret;
	struct starpu_conf conf;

#ifdef STARPU_HAVE_UNSETENV
	unsetenv("STARPU_SCHED");
#endif

	starpu_conf_init(&conf);
	conf.sched_policy = &dummy_sched_policy,
	ret = starpu_init(&conf);
	if (ret == -ENODEV)
		return 77;
	STARPU_CHECK_RETURN_VALUE(ret, "starpu_init");

#ifdef STARPU_QUICK_CHECK
	ntasks /= 100;
#endif

	int i;
	for (i = 0; i < ntasks; i++)
	{
		struct starpu_task *task = starpu_task_create();

		task->cl = &dummy_codelet;
		task->cl_arg = NULL;

		ret = starpu_task_submit(task);
		STARPU_CHECK_RETURN_VALUE(ret, "starpu_task_submit");
	}

	starpu_task_wait_for_all();

	starpu_shutdown();

	return 0;
}
