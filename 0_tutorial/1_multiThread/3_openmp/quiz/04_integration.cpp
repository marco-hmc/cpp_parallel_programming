#include <omp.h>

#include <cmath>
#include <iostream>

int main() {
    const int num_steps = 1000000;
    double step = 1.0 / num_steps;
    double full_sum = 0.0;

#pragma omp parallel
    {
        int id = omp_get_thread_num();
        int numthreads = omp_get_num_threads();
        double x = NAN;
        double partial_sum = 0.0;

#pragma omp single
        std::cout << " num_threads = " << numthreads << std::endl;

        for (int i = id; i < num_steps; i += numthreads) {
            x = (i + 0.5) * step;
            partial_sum += 4.0 / (1.0 + x * x);
        }

#pragma omp critical
        full_sum += partial_sum;
    }

    double pi = step * full_sum;
    std::cout << "Calculated PI = " << pi << std::endl;

    return 0;
}
