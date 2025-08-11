#pragma once

// Bubble sort of integers
static void sort_int(int* a, int N)
{
    int i, j;
    int temp;
    for (i = 0; i < (N - 1); ++i)
    {
        for (j = 0; j < N - 1 - i; ++j)
        {
            int cond1 = a[j] > a[j + 1];
            if (cond1)
            {
                temp     = a[j + 1];
                a[j + 1] = a[j];
                a[j]     = temp;
            }
        }
    }
}