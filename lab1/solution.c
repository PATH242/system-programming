#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "libcoro.h"

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

};

int* ReadNumsFromFile(char* name, int* numsVector, int* size, int* capacity){
    char filename[11];
    sprintf(filename, "test%c.txt", name[5]);
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
	return ctx;
}

static void
my_context_delete(struct my_context *ctx)
{
	free(ctx->name);
    // free(ctx->numsVector);
    // free(ctx->size);
    free(ctx->capacity);
	// free(ctx);
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

void QuickSort(int *numsVector, int s, int e, struct coro* this, char* name)
{
    if (e <= s)
    {
        return;
    }
    int pivot = QuickSortHelper(numsVector, s, e);
    printf("%s: switch count %lld\n", name, coro_switch_count(this));
	printf("%s: yield\n", name);
	coro_yield();
    QuickSort(numsVector, s, pivot - 1, this, name);
    QuickSort(numsVector, pivot + 1, e, this, name);
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

	printf("%s: switch count %lld\n", name, coro_switch_count(this));
    QuickSort(ctx->numsVector, 0, (*ctx->size) - 1, this, name);
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
    printf("%d\n", size);
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

int main(int argc, char **argv)
{
	coro_sched_init();
    struct my_context** contexts = malloc(6 * sizeof(struct my_context*));
    int lst = 0;
	/* Start several coroutines. */
    /* Each file should be sorted in its own coroutine*/
	for (int i = 1; i < 7; ++i) {
		char name[16];
		sprintf(name, "coro_%d", i);
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
    for(int i = 0; i < 6; i ++){
        size += *contexts[i]->size;
    }
    printf("%d numbers have been sorted\n", size);
    int* resultVector = (int*) malloc(size * sizeof(int));
    resultVector = MergeSortedArrays(contexts, 6);

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
	return 0;
}