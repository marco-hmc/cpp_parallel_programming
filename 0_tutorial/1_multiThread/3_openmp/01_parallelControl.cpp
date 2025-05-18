#include <omp.h>

#include <iostream>

namespace OpenMP_1_parallel {
    void task() {
#pragma omp parallel
        {
            int thread_id = omp_get_thread_num();
            std::cout << "Thread " << thread_id << " is running." << std::endl;
        }
    }
}  // namespace OpenMP_1_parallel

namespace OpenMP_2_parallelFor {
    void task() {
        const int size = 5;
        int a[size];

#pragma omp parallel for
        for (int i = 0; i < size; i++) {
            a[i] = i;
        }

        for (int i : a) {
            std::cout << i << std::endl;
        }
    }
}  // namespace OpenMP_2_parallelFor

namespace OpenMP_3_parallelSections {
    void task() {
        omp_set_num_threads(4);

#pragma omp parallel sections
        {
#pragma omp section
            {
                int thread_id = omp_get_thread_num();
                std::cout << "Section 1 executed by thread " << thread_id
                          << std::endl;
            }

#pragma omp section
            {
                int thread_id = omp_get_thread_num();
                std::cout << "Section 2 executed by thread " << thread_id
                          << std::endl;
            }

#pragma omp section
            {
                int thread_id = omp_get_thread_num();
                std::cout << "Section 3 executed by thread " << thread_id
                          << std::endl;
            }

#pragma omp section
            {
                int thread_id = omp_get_thread_num();
                std::cout << "Section 4 executed by thread " << thread_id
                          << std::endl;
            }
        }
    }
}  // namespace OpenMP_3_parallelSections

namespace OpenMP_4_scheduling {
    /*
    scheduling:
      static  -> statically preassign iterations to threads
      dynamic -> each thread gets more work when its done at runtime
      guided  -> similar to dynamic with automatically adjusted chunk size
      auto    -> let the compiler decide!
    */

#define CHUNK_SIZE 5
    void task() {
        const int niter = 25;

#pragma omp parallel for schedule(static, CHUNK_SIZE)
        for (int i = 0; i < niter; i++) {
            int thr = omp_get_thread_num();
            printf("iter %d of %d on thread %d\n", i, niter, thr);
        }
    }
}  // namespace OpenMP_4_scheduling

namespace OpenMP_5_master {
    void task() {
#pragma omp parallel
        {
            int thread_id = omp_get_thread_num();

#pragma omp master
            {
                std::cout << "Master thread (ID: " << thread_id
                          << ") is executing this block." << std::endl;
            }

            std::cout << "Thread " << thread_id << " is running." << std::endl;
        }
    }
}  // namespace OpenMP_5_master

namespace OpenMP_6_simd {
    void task() {
        const int N = 1000;
        int a[N], b[N], c[N];

        for (int i = 0; i < N; i++) {
            a[i] = i;
            b[i] = i * 2;
        }

#pragma omp simd
        for (int i = 0; i < N; i++) {
            c[i] = a[i] + b[i];
        }

        for (int i = 0; i < 10; i++) {
            std::cout << "c[" << i << "] = " << c[i] << std::endl;
        }
    }
}  // namespace OpenMP_6_simd

namespace OpenMP_7_task {
    void task() {
#pragma omp parallel
        {
#pragma omp single
            {
                std::cout << "Starting tasks..." << std::endl;

#pragma omp task
                {
                    std::cout << "Task 1 executed by thread "
                              << omp_get_thread_num() << std::endl;
                }

#pragma omp task
                {
                    std::cout << "Task 2 executed by thread "
                              << omp_get_thread_num() << std::endl;
                }

#pragma omp taskwait
                std::cout << "All tasks completed." << std::endl;
            }
        }
    }
}  // namespace OpenMP_7_task

namespace OpenMP_8_taskLoop {
    void task() {
        const int N = 10;
        int a[N], b[N], c[N];

        for (int i = 0; i < N; ++i) {
            a[i] = i;
            b[i] = i * 2;
        }

#pragma omp parallel
        {
#pragma omp single
            {
#pragma omp taskloop
                for (int i = 0; i < N; ++i) {
                    c[i] = a[i] + b[i];
                    std::cout << "Task " << i << " executed by thread "
                              << omp_get_thread_num() << std::endl;
                }
            }
        }

        std::cout << "Result array: ";
        for (int i = 0; i < N; ++i) {
            std::cout << c[i] << " ";
        }
        std::cout << std::endl;
    }
}  // namespace OpenMP_8_taskLoop

namespace OpenMP_9_target {
    void task() {
        const int N = 1000;
        int a[N], b[N], c[N];

        for (int i = 0; i < N; i++) {
            a[i] = i;
            b[i] = i * 2;
        }

#pragma omp target map(to : a, b) map(from : c)
        {
            for (int i = 0; i < N; i++) {
                c[i] = a[i] + b[i];
            }
        }

        for (int i = 0; i < 10; i++) {
            std::cout << "c[" << i << "] = " << c[i] << std::endl;
        }
    }
}  // namespace OpenMP_9_target

namespace OpenMP_10_teams {
    void task() {
        const int N = 10;
        int a[N], b[N], c[N];

        for (int i = 0; i < N; ++i) {
            a[i] = i;
            b[i] = i * 2;
        }

#pragma omp target teams distribute parallel for
        for (int i = 0; i < N; ++i) {
            c[i] = a[i] + b[i];
        }

        for (int i = 0; i < N; ++i) {
            std::cout << "c[" << i << "] = " << c[i] << std::endl;
        }
    }
}  // namespace OpenMP_10_teams

namespace OpenMP_11_distribute {
    void task() {
        const int N = 100;
        int a[N], b[N], c[N];

        for (int i = 0; i < N; ++i) {
            a[i] = i;
            b[i] = 2 * i;
        }

#pragma omp parallel
        {
#pragma omp single
            {
#pragma omp distribute
                for (int i = 0; i < N; ++i) {
                    c[i] = a[i] + b[i];
                }
            }
        }

        for (int i = 0; i < N; ++i) {
            std::cout << "c[" << i << "] = " << c[i] << std::endl;
        }
    }
}  // namespace OpenMP_11_distribute

namespace OpenMP_12_nested {
    void task() {
        omp_set_nested(1);

#pragma omp parallel num_threads(2)
        {
            printf("Level 1, thread %d of %d\n", omp_get_thread_num(),
                   omp_get_num_threads());
#pragma omp parallel num_threads(2)
            {
                printf("Level 2, thread %d of %d\n", omp_get_thread_num(),
                       omp_get_num_threads());
            }
        }
    }
}  // namespace OpenMP_12_nested

int main() {
    OpenMP_1_parallel::task();
    OpenMP_2_parallelFor::task();
    OpenMP_3_parallelSections::task();
    OpenMP_4_scheduling::task();
    OpenMP_5_master::task();
    OpenMP_6_simd::task();
    OpenMP_7_task::task();
    OpenMP_8_taskLoop::task();
    OpenMP_9_target::task();
    OpenMP_10_teams::task();
    OpenMP_11_distribute::task();
    OpenMP_12_nested::task();
    return 0;
}