#include "thread_pool.h"
#include <pthread.h>
#include <time.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

typedef struct thread_task
{
	thread_task_f function;
	void *arg;
	bool is_in_pool;
	bool is_finished;
	void *result;
	pthread_mutex_t finished_mutex;
	pthread_cond_t finished_cond;
	struct thread_task* next; 
} thread_task;

typedef struct thread_pool
{
	pthread_t **threads;
	int active_threads;
	int total_threads;
	thread_task* tasks_queue;
	thread_task* tasks_queue_end;
	int tasks_left;
	int queue_size;
	int delete_pool;
	int active_tasks;
	pthread_mutex_t queue_mutex;
	pthread_mutex_t tasks_left_mutex;
	pthread_mutex_t active_tasks_mutex;
	pthread_cond_t queue_cond;
} thread_pool;

int thread_pool_new(int max_thread_count, struct thread_pool **pool)
{

	if (max_thread_count > TPOOL_MAX_THREADS || max_thread_count < 1)
	{
		return TPOOL_ERR_INVALID_ARGUMENT;
	}
	*pool = (thread_pool *)malloc(sizeof(thread_pool));
	(*pool)->threads = (pthread_t **)malloc(sizeof(pthread_t *) * max_thread_count);
	(*pool)->active_threads = 0;
	(*pool)->total_threads = max_thread_count;
	(*pool)->tasks_queue = NULL;
	(*pool)->tasks_queue_end = NULL;
	(*pool)->queue_size = 0;
	(*pool)->tasks_left = 0;
	(*pool)->delete_pool = 0;
	(*pool)->active_tasks = 0;
	pthread_mutex_init(&(*pool)->queue_mutex, NULL);
	pthread_mutex_init(&(*pool)->active_tasks_mutex, NULL);
	pthread_mutex_init(&(*pool)->tasks_left_mutex, NULL);
	pthread_cond_init(&(*pool)->queue_cond, NULL);
	return 0;
}

int thread_pool_thread_count(const struct thread_pool *pool)
{
	return pool->active_threads;
}

int thread_pool_delete(struct thread_pool *pool)
{
	if (pool->tasks_left || pool->active_tasks)
	{
		return TPOOL_ERR_HAS_TASKS;
	}

	pool->delete_pool = 1;
	pthread_cond_broadcast(&(pool->queue_cond));
	for (int i = 0; i < pool->active_threads; i++)
	{
		pthread_join(*pool->threads[i], NULL);
		free(pool->threads[i]);
		pool->threads[i] = NULL;
	}

	// free(pool->tasks_queue);
	free(pool->threads);
	pthread_mutex_destroy(&(pool->queue_mutex));
	pthread_mutex_destroy(&(pool->active_tasks_mutex));
	pthread_mutex_destroy(&(pool->tasks_left_mutex));
	pthread_cond_destroy(&(pool->queue_cond));
	free(pool);
	return 0;
}

void execute_task(thread_task *task)
{
	if (task)
	{
		task->result = task->function(task->arg);
	}
	task->is_finished = true;
	task->is_in_pool = false;
}

void *thread_pool_execute(void *void_pool)
{
	thread_pool *pool = (thread_pool *)void_pool;
	while (!pool->delete_pool)
	{
		pthread_mutex_lock(&(pool->queue_mutex));
		while (!pool->delete_pool && pool->tasks_left == 0)
		{
			pthread_cond_wait(&(pool->queue_cond), &(pool->queue_mutex));
		}

		if (pool->tasks_left)
		{
			thread_task *current_task = pool->tasks_queue;
			pool->tasks_queue = pool->tasks_queue->next;
			if(pool->tasks_queue == NULL)
			{
				pool->tasks_queue_end = NULL;
			}
			pthread_mutex_lock(&pool->tasks_left_mutex);
			pool->tasks_left--;
			pthread_mutex_unlock(&pool->tasks_left_mutex);

			pthread_mutex_lock(&pool->active_tasks_mutex);
			pool->active_tasks++;
			pthread_mutex_unlock(&pool->active_tasks_mutex);

			pthread_mutex_unlock(&(pool->queue_mutex));
			pthread_mutex_lock(&current_task->finished_mutex);
			execute_task(current_task);
			pthread_mutex_lock(&pool->active_tasks_mutex);
			pool->active_tasks--;
			pthread_mutex_unlock(&pool->active_tasks_mutex);
			// Signal to task that it was executed: for join
			pthread_cond_signal(&(current_task->finished_cond));
			pthread_mutex_unlock(&current_task->finished_mutex);
		}
		else
		{
			pthread_mutex_unlock(&(pool->queue_mutex));	
		}
	}
	pthread_exit(NULL);
	return NULL;
}

int thread_pool_push_task(struct thread_pool *pool, struct thread_task *task)
{
	if (pool->tasks_left >= TPOOL_MAX_TASKS)
	{
		return TPOOL_ERR_TOO_MANY_TASKS;
	}
	task->is_in_pool = true;
	task->is_finished = false;
	pthread_mutex_lock(&(pool->queue_mutex));
	if(pool->tasks_left)
	{
		pool->tasks_queue_end->next = task;
		pool->tasks_queue_end = task;
		task->next = NULL;
	}
	else
	{
		pool->tasks_queue_end = pool->tasks_queue = task;
		task->next = NULL;
	}
	pthread_mutex_lock(&pool->tasks_left_mutex);	
	pool->tasks_left++;
	pthread_mutex_unlock(&pool->tasks_left_mutex);

	// Gradually start threads as more tasks are pushed.
	if (pool->active_threads < pool->tasks_left && pool->active_threads < pool->total_threads)
	{
		pool->threads[pool->active_threads] = malloc(sizeof(pthread_t));
		if (pthread_create(pool->threads[pool->active_threads],
						   NULL, thread_pool_execute, pool))
		{
			printf("Thread creation failed\n");
		}
		else
		{
			pool->active_threads++;
		}
	}
	pthread_cond_signal(&(pool->queue_cond));
	pthread_mutex_unlock(&(pool->queue_mutex));
	return 0;
}

int thread_task_new(struct thread_task **task, thread_task_f function, void *arg)
{
	(*task) = malloc(sizeof(thread_task));
	(*task)->function = function;
	(*task)->arg = arg;
	(*task)->is_in_pool = false;
	(*task)->is_finished = false;
	pthread_cond_init(&(*task)->finished_cond, NULL);
	pthread_mutex_init(&(*task)->finished_mutex, NULL);
	return 0;
}

int thread_task_join(struct thread_task *task, void **result)
{
	if (!task->is_in_pool && !task->is_finished)
	{
		return TPOOL_ERR_TASK_NOT_PUSHED;
	}
	pthread_mutex_lock(&(task->finished_mutex));
	while (!task->is_finished)
	{
		pthread_cond_wait(&(task->finished_cond), &(task->finished_mutex));
	}
	pthread_mutex_unlock(&(task->finished_mutex));
	(*result) = task->result;
	return 0;
}

#ifdef NEED_TIMED_JOIN

int thread_task_timed_join(struct thread_task *task, double timeout, void **result)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	(void)timeout;
	(void)result;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif

int thread_task_delete(struct thread_task *task)
{

	if (task != NULL && task->is_in_pool)
	{
		return TPOOL_ERR_TASK_IN_POOL;
	}

	if (task != NULL)
	{
		pthread_mutex_destroy(&(task->finished_mutex));
		pthread_cond_destroy(&(task->finished_cond));
		free(task);
	}

	return 0;
}

#ifdef NEED_DETACH

int thread_task_detach(struct thread_task *task)
{
	/* IMPLEMENT THIS FUNCTION */
	(void)task;
	return TPOOL_ERR_NOT_IMPLEMENTED;
}

#endif
