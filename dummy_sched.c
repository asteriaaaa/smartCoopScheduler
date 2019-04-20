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

#ifdef STARPU_QUICK_CHECK
#define NTASKS	320
#elif !defined(STARPU_LONG_CHECK)
#define NTASKS	3200
#else
#define NTASKS	32000
#endif
#define FPRINTF(ofile, fmt, ...) do { if (!getenv("STARPU_SSILENT")) {fprintf(ofile, fmt, ## __VA_ARGS__); }} while(0)

struct dummy_sched_data
{
	struct starpu_task_list sched_list;
    starpu_pthread_mutex_t policy_mutex;
	struct starpu_task_list *worker_sched_list;
};

static void init_dummy_sched(unsigned sched_ctx_id)
{
	starpu_sched_ctx_create_worker_collection(sched_ctx_id, STARPU_WORKER_LIST);

	struct dummy_sched_data *data = (struct dummy_sched_data*)malloc(sizeof(struct dummy_sched_data));

	unsigned int worker_num = starpu_worker_get_count();

    data->worker_sched_list = (struct starpu_task_list*) malloc ((worker_num)*sizeof(struct starpu_task_list));
	/* Create a linked-list of tasks and a condition variable to protect it */
	starpu_task_list_init(&data->sched_list);
    for (unsigned int i = 0; i < worker_num; i++){
        starpu_task_list_init(&data->worker_sched_list[i]);
    } 

	starpu_sched_ctx_set_policy_data(sched_ctx_id, (void*)data);

	STARPU_PTHREAD_MUTEX_INIT(&data->policy_mutex, NULL);
	FPRINTF(stderr, "Initialising Dummy scheduler\n");
}

static void deinit_dummy_sched(unsigned sched_ctx_id)
{
	struct dummy_sched_data *data = (struct dummy_sched_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	STARPU_ASSERT(starpu_task_list_empty(&data->sched_list));
	unsigned int worker_num = starpu_worker_get_count();
    for (unsigned int i = 0; i < worker_num; i++){
        STARPU_ASSERT(starpu_task_list_empty(&data->worker_sched_list[i]));
    }
	starpu_sched_ctx_delete_worker_collection(sched_ctx_id);

	STARPU_PTHREAD_MUTEX_DESTROY(&data->policy_mutex);

	free(data);

	FPRINTF(stderr, "Destroying Dummy scheduler\n");
}

static int push_task_on_device(unsigned sched_ctx_id){
    struct dummy_sched_data *data = (struct dummy_sched_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
    struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);
    struct starpu_task *task = starpu_task_list_pop_front(&data->sched_list);
	//fprintf(stdout, "Hello");
    struct starpu_sched_ctx_iterator it;
    //workers->init_iterator(workers, &it);
    unsigned worker;
    int ran =rand()%3;
    starpu_task_list_push_back(&data->worker_sched_list[ran], task); //TODO
    return 0;
}

static int push_task_dummy(struct starpu_task *task)
{
	unsigned sched_ctx_id = task->sched_ctx;
	struct dummy_sched_data *data = (struct dummy_sched_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);

	/* NB: In this simplistic strategy, we assume that the context in which
	   we push task has at least one worker*/


	/* lock all workers when pushing tasks on a list where all
	   of them would pop for tasks */
    STARPU_PTHREAD_MUTEX_LOCK(&data->policy_mutex);

	starpu_task_list_push_back(&data->sched_list, task);
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
static double get_task_heter_ration(unsigned sched_ctx_id)
{
	struct starpu_worker_collection *workers = starpu_sched_ctx_get_worker_collection(sched_ctx_id);

	struct starpu_sched_ctx_iterator it;
	struct starpu_sched_ctx_iterator it1;
	double max_execution_time = 0;
	double max_heter_tatio = 0;


	workers->init_iterator(workers, &it);
	while(workers->has_next_master(workers, &it))
	{
		worker = workers->get_next_master(workers, &it);
		unsigned memory_node = starpu_worker_get_memory_node(worker);
		struct starpu_perfmodel_arch* perf_arch = starpu_worker_get_perf_archtype(worker, sched_ctx_id);
		if (!starpu_worker_can_execute_task_impl(worker, task, &impl_mask))
			continue;

		for (nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
		{
			if (!(impl_mask & (1U << nimpl)))
			{
				/* no one on that queue may execute this task */
				continue;
			}
			double local_length = starpu_task_expected_length(task, perf_arch, nimpl);
			if (local_length>max_execution_time) max_execution_time = local_length;
		}
	}

	workers->init_iterator(workers, &it1);
	while(workers->has_next_master(workers, &it1))
	{
		worker = workers->get_next_master(workers, &it1);
		unsigned memory_node = starpu_worker_get_memory_node(worker);
		struct starpu_perfmodel_arch* perf_arch = starpu_worker_get_perf_archtype(worker, sched_ctx_id);
		if (!starpu_worker_can_execute_task_impl(worker, task, &impl_mask))
			continue;

		for (nimpl = 0; nimpl < STARPU_MAXIMPLEMENTATIONS; nimpl++)
		{
			if (!(impl_mask & (1U << nimpl)))
			{
				/* no one on that queue may execute this task */
				continue;
			}
			double local_length = starpu_task_expected_length(task, perf_arch, nimpl);
			double heter_ratio = max_execution_time/local_length;
			if(heter_ratio>max_heter_tatio)max_heter_tatio = heter_ratio;
		}
	}
	return max_heter_tatio;
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
	struct dummy_sched_data *data = (struct dummy_sched_data*)starpu_sched_ctx_get_policy_data(sched_ctx_id);
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
	.policy_description = "dummy scheduling strategy"
};

void dummy_func(void *descr[] STARPU_ATTRIBUTE_UNUSED, void *arg STARPU_ATTRIBUTE_UNUSED)
{
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
