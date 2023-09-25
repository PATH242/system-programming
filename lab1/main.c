#include <stdio.h>
#include <stdlib.h>

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

void QuickSort(int *numsVector, int s, int e)
{
    if (e <= s)
    {
        return;
    }
    int pivot = QuickSortHelper(numsVector, s, e);
    QuickSort(numsVector, s, pivot - 1);
    QuickSort(numsVector, pivot + 1, e);
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

int* ReadNumsFromFile(char filename[], int* numsVector, int* size, int* capacity){
    numsVector = (int *)malloc( (*capacity) * sizeof(int));
    FILE *test_file = fopen(filename, "r");
    if (test_file == NULL)
    {
        printf("Error: FILE NOT FOUND\n");
        return 1;
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
                return -1;
            }
        }
        numsVector[(*size)++] = num;
    }
    fclose(test_file);
    return numsVector;
}

int main()
{
    int *numsVector1, *numsVector2;
    int size1 = 0, size2 = 0;
    int capacity1 = 2, capacity2 = 2;
    // TODO: assert that read doesn't return nullptr
    numsVector1 = ReadNumsFromFile("test1.txt", numsVector1, &size1, &capacity1);
    QuickSort(numsVector1, 0, size1 - 1);

    numsVector2 = ReadNumsFromFile("test2.txt", numsVector2, &size2, &capacity2);
    QuickSort(numsVector2, 0, size2 - 1);
    
    int* resultVector = (int *) malloc( (size1 + size2 + 5) * sizeof(int));
    merge(numsVector1, numsVector2, size1, size2, resultVector);

    int size = size1 + size2;
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