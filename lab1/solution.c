#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"
#include <time.h>

/**
The most practical sorting algorithm is a hybrid of algorithms; 
So, this code will use quick sort for individual files and the 
merge functionality of merge sort for merging already sorted arrays. 
*/

struct my_context {
	char *name;
	/* TODO: ADD HERE WORK TIME, ... */
    int* numsVector;
    int* size;
    int* capacity;
    struct timespec start_time;
    struct timespec end_time;
    long long int total_work_time_nsec;
    int context_switch_count;

};

int* ReadNumsFromFile(char* filename, int* numsVector, int* size, int* capacity){
    *capacity = 2;
    *size = 0;
    numsVector = (int *)malloc( (*capacity) * sizeof(int));
    FILE *test_file = fopen(filename, "r");
    if (test_file == NULL)
    {
        printf("Error: FILE NOT FOUND\n");
        return NULL;
    }
    int num;
    while (fscanf(test_file, "%d", &num) != EOF)
    {
        if ((*size) == (*capacity) )
        {
            (*capacity) *= 2;
            numsVector = (int *)realloc(numsVector, (*capacity) * sizeof(int));
            if (numsVector == NULL)
            {
                printf("Error: MEMORY ALLOCATION FAILED\n");
                return NULL;
            }
        }
        numsVector[(*size)++] = num;
    }
    fclose(test_file);
    return numsVector;
}

static struct my_context *
my_context_new(const char *name)
{
	struct my_context *ctx = malloc(sizeof(*ctx));
	ctx->name = strdup(name);
    ctx->size = (int*)malloc(sizeof(int));
    ctx->capacity = (int*)malloc(sizeof(int));
    ctx->numsVector = 
        ReadNumsFromFile(ctx->name, ctx->numsVector, ctx->size, ctx->capacity);
    ctx->context_switch_count = 0;
	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
    free(ctx->capacity);
}

// Utility functions for sorting 

void swap(int *a, int *b)
{
    int t = *a;
    *a = *b;
    *b = t;
}

int GetMedianPivotIndex(int *numsVector, int s, int e)
{
    int mid = (s + e) / 2;

    if (numsVector[s] > numsVector[mid])
    {
        if (numsVector[mid] > numsVector[e])
        {
            return mid;
        }
        else if (numsVector[s] > numsVector[e])
        {
            return e;
        }
        else
        {
            return s;
        }
    }
    else
    {
        if (numsVector[s] > numsVector[e])
        {
            return s;
        }
        else if (numsVector[mid] > numsVector[e])
        {
            return e;
        }
        else
        {
            return mid;
        }
    }
}

// Main sorting algorithm used for each file.

int QuickSortHelper(int *numsVector, int s, int e)
{
    int pivotIndex = GetMedianPivotIndex (numsVector, s, e);
    int pivotValue = *(numsVector + pivotIndex);

    swap(numsVector + pivotIndex, numsVector + e);

    int i = (s - 1);

    for (int j = s; j <= e - 1; j++)
    {
        if (*(numsVector + j) < pivotValue)
        {
            i++;
            swap(numsVector + i, numsVector + j);
        }
    }
    swap((numsVector + i + 1), numsVector + e);
    return (i + 1);
}

// Functionality: Sorts a list of numbers.
// Returns: time wasted during yield to be subtracted from total time of running of this function.
long long int QuickSort(int *numsVector, int s, int e, struct coro* this, char* name, struct my_context *ctx)
{
    if (e <= s)
    {
        return 0;
    }
    int pivot = QuickSortHelper(numsVector, s, e);

    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);
    long long int yield_start_time = start_time.tv_sec * 1000000000 + start_time.tv_nsec;
    
    ctx->context_switch_count += coro_switch_count(this);
    coro_yield();

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    
    long long int yield_end_time = end_time.tv_sec * 1000000000 + end_time.tv_nsec;
    long long int yield_total_time = yield_end_time - yield_start_time;

    yield_total_time += QuickSort(numsVector, s, pivot - 1, this, name, ctx);
    yield_total_time += QuickSort(numsVector, pivot + 1, e, this, name, ctx);
    return yield_total_time;
}

/**
 * Coroutine body. This code is executed by all the coroutines. Here you
 * implement your solution, sort each individual file.
 */

static int
coroutine_func_f(void *context)
{
	/* IMPLEMENT SORTING OF wqsa FILES HERE. */

	struct coro *this = coro_this();
	struct my_context *ctx = context;
	char *name = ctx->name;
	printf("Started coroutine %s\n", name);
    
    clock_gettime(CLOCK_MONOTONIC, &ctx->start_time);     
    long long int yield_time = QuickSort(ctx->numsVector, 0, (*ctx->size) - 1, this, name, ctx);
    clock_gettime(CLOCK_MONOTONIC, &ctx->end_time); 

    long long int work_time_nsec = (ctx->end_time.tv_sec - ctx->start_time.tv_sec) * 1000000000 +
                            (ctx->end_time.tv_nsec - ctx->start_time.tv_nsec) - yield_time;
    
    __sync_add_and_fetch(&ctx->total_work_time_nsec, work_time_nsec);

	printf("%s: switch count after other function %lld\n", name,
	       coro_switch_count(this));

	my_context_delete(ctx);
	/* This will be returned from coro_status(). */
	return 0;
}


void merge(int* arr1, int* arr2, int size1, int size2, int* result){

    int i = 0, j = 0, k = 0;

    while (i < size1 && j < size2) {
        if (*(arr1 + i) < *(arr2+ j) ) {
            *(result + k++) = *(arr1 + i++);
        } else {
            *(result + k++) = *(arr2 + j++);
        }
    }

    while (i < size1) {
        *(result + k++) = *(arr1 + i++);
    }
    while (j < size2) {
       *(result + k++) = *(arr2 + j++);
    }
}

int *MergeSortedArrays(struct my_context **contexts, int size)
{
    if (size == 1)
    {
        return contexts[0]->numsVector;
    }
    struct my_context **new_contexts = malloc((size / 2 + 1) * sizeof(struct my_context *));

    for (int i = 0; i < size - 1; i += 2)
    {
        int *new_size = malloc(sizeof(int));
        *new_size = (*contexts[i]->size) + (*contexts[i + 1]->size);
        int *resultVector = (int *)malloc((*new_size) * sizeof(int));
        if (resultVector == NULL)
        {
            printf("Error: MEMORY ALLOCATION FAILED\n");
            return NULL;
        }

        merge(contexts[i]->numsVector, contexts[i + 1]->numsVector, *contexts[i]->size, *contexts[i + 1]->size, resultVector);

        struct my_context *new_context = (struct my_context *)malloc(sizeof(struct my_context));

        new_context->numsVector = resultVector;
        new_context->size = new_size;
        new_contexts[i/2 + (i%2)] = new_context;
    }
    if ((size % 2))
    {
        new_contexts[size / 2] = contexts[size - 1];
    }
    MergeSortedArrays(new_contexts, (size / 2 + (size % 2)));
}

// The following code assumes valid input only.
// EX: ./a.out test1.txt test2.txt test3.txt test4.txt
int main(int argc, char **argv)
{
	coro_sched_init();
    struct my_context** contexts = malloc( (argc - 1) * sizeof(struct my_context*));
    int lst = 0;
	/* Start several coroutines. */
    /* Each file should be sorted in its own coroutine*/
	for (int i = 1; i < argc; ++i) {
		char name[16];
		sprintf(name, argv[i]);
        contexts[lst++] = my_context_new(name);
        coro_new(coroutine_func_f, contexts[lst-1]);
	}
    /* Wait for all the coroutines to end. */
	struct coro *c;
	while ((c = coro_sched_wait()) != NULL) {
		printf("Finished %d\n", coro_status(c));
		coro_delete(c);
	}
	/* All coroutines have finished. */

	/* MERGING OF THE SORTED ARRAYS */

    int size = 0;
    for(int i = 0; i < argc - 1; i ++){
        size += *contexts[i]->size;
    }
    printf("%d numbers have been sorted\n", size);
    int* resultVector = (int*) malloc(size * sizeof(int));
    resultVector = MergeSortedArrays(contexts, (argc - 1));

    long long int total_work_time_nsec = 0;
    long long int total_context_switches = 0;

    for (int i = 0; i < argc - 1; i++) {
        total_work_time_nsec += contexts[i]->total_work_time_nsec;
        total_context_switches += contexts[i]->context_switch_count;
    }

    // Rest of my_context_delete
    for(int i = 0; i < argc - 1; i ++){
        free(contexts[i]->size);
        free(contexts[i]->numsVector);
	    free(contexts[i]);
    }

    FILE * output_file = fopen("result.txt", "w");
    if (output_file == NULL) {
        printf("Could not open the file for writing.\n");
        return 1;
    }

    for (int i = 0; i < size; i++) {
        fprintf(output_file, "%d", resultVector[i]);

        if (i < size - 1) {
            fprintf(output_file, " ");
        } else {
            fprintf(output_file, "\n");
        }
    }

    fclose(output_file);

    printf("Total Work Time (ns): %lld\n", total_work_time_nsec);
    printf("Context Switches: %lld\n", total_context_switches);

	return 0;
}