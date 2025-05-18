#include <omp.h>

#include <iostream>
#include <vector>

#define N 10000
#define Nthreads 2

/* Some random number constants from numerical recipies */
#define SEED 2531
#define RAND_MULT 1366
#define RAND_ADD 150889
#define RAND_MOD 714025

int randy = SEED;

/* function to fill an array with random numbers */
void fill_rand(int length, std::vector<double>& a) {
    for (int i = 0; i < length; i++) {
        randy = (RAND_MULT * randy + RAND_ADD) % RAND_MOD;
        a[i] = static_cast<double>(randy) / static_cast<double>(RAND_MOD);
    }
}

/* function to sum the elements of an array */
double sum_array(int length, const std::vector<double>& a) {
    double sum = 0.0;
    for (int i = 0; i < length; i++) {
        sum += a[i];
    }
    return sum;
}

int main() {
    std::vector<double> A(N);
    double sum = 0.0;
    double runtime = 0.0;
    int numthreads = 0;
    int flag = 0;

    omp_set_num_threads(Nthreads);

#pragma omp parallel
    {
#pragma omp master
        {
            numthreads = omp_get_num_threads();
            if (numthreads != 2) {
                std::cerr << "error: incorrect number of threads, "
                          << numthreads << std::endl;
                exit(-1);
            }
            runtime = omp_get_wtime();
        }
#pragma omp barrier

#pragma omp sections
        {
#pragma omp section
            {
                fill_rand(N, A);
#pragma omp flush
                flag = 1;
#pragma omp flush(flag)
            }
#pragma omp section
            {
#pragma omp flush(flag)
                while (flag != 1) {
#pragma omp flush(flag)
                }

#pragma omp flush
                sum = sum_array(N, A);
            }
        }
#pragma omp master
        runtime = omp_get_wtime() - runtime;
    }

    std::cout << "With " << numthreads << " threads and " << runtime
              << " seconds, the sum is " << sum << std::endl;

    return 0;
}