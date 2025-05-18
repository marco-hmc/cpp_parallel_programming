#include <omp.h>
#include <unistd.h>

#include <cstdio>
#include <iostream>

namespace OpenMP_1_critical {
    void task() {
        int sum = 0;

#pragma omp parallel for
        for (int i = 0; i < 10; ++i) {
#pragma omp critical
            {
                sum += i;
                std::cout << "Thread " << omp_get_thread_num() << " added " << i
                          << ", current sum: " << sum << std::endl;
            }
        }

        std::cout << "Final sum: " << sum << std::endl;
    }
}  // namespace OpenMP_1_critical

namespace OpenMP_2_atomic {
    double two_body_energy(int i, int j) {
        return (2.0 * i + 3.0 * j) / 10.0;  // some dummy return value
    }

    void task() {
        const int nbodies = 1000;
        double energy = 0.0;

#pragma omp parallel for
        for (int i = 0; i < nbodies; i++) {
            for (int j = i + 1; j < nbodies; j++) {
                double eij = two_body_energy(i, j);
#pragma omp atomic
                energy += eij;
            }
        }

        std::cout << "energy = " << energy << std::endl;
    }
}  // namespace OpenMP_2_atomic

namespace OpenMP_3_barrier {
    void task() {
#pragma omp parallel
        {
            printf("Hello from thread %d of %d\n", omp_get_thread_num(),
                   omp_get_num_threads());
#pragma omp barrier  // all threads wait here
            printf("Thread %d of %d have passed the barrier\n",
                   omp_get_thread_num(), omp_get_num_threads());
        }
    }
}  // namespace OpenMP_3_barrier

namespace OpenMP_4_ordered {
    void task() {
        const int niter = 10;

#pragma omp parallel for ordered  // loop must be marked as ordered
        for (int i = 0; i < niter; i++) {
            int thr = omp_get_thread_num();
            printf("unordered iter %d of %d on thread %d\n", i, niter, thr);
#pragma omp ordered
            printf("ordered iter %d of %d on thread %d\n", i, niter, thr);
        }
    }
}  // namespace OpenMP_4_ordered

namespace OpenMP_5_single {
    void task() {
#pragma omp parallel
        {
#pragma omp single
            {
                // 仅由一个线程执行的代码块
                printf("This is executed by a single thread.\n");
            }
#pragma omp barrier
#pragma omp critical
            {
                // 由每个线程执行的代码块
                printf("This is executed by thread %d\n", omp_get_thread_num());
            }
        }
    }
}  // namespace OpenMP_5_single

namespace OpenMP_6_taskWait {
    void task() {
        int x = 0, y = 0;

#pragma omp parallel
        {
#pragma omp single
            {
#pragma omp task
                {
                    x = 1;
                    std::cout << "Task 1 executed by thread "
                              << omp_get_thread_num() << std::endl;
                }

#pragma omp task
                {
                    y = 2;
                    std::cout << "Task 2 executed by thread "
                              << omp_get_thread_num() << std::endl;
                }

#pragma omp taskwait

                std::cout << "Both tasks completed. x = " << x << ", y = " << y
                          << std::endl;
            }
        }
    }
}  // namespace OpenMP_6_taskWait

namespace OpenMP_7_taskGroup {
    void task() {
        omp_set_num_threads(4);

#pragma omp parallel
        {
#pragma omp single
            {
                std::cout << "Starting task group" << std::endl;

#pragma omp taskgroup
                {
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

#pragma omp task
                    {
                        std::cout << "Task 3 executed by thread "
                                  << omp_get_thread_num() << std::endl;
                    }
                }

                std::cout << "Task group completed" << std::endl;
            }
        }
    }
}  // namespace OpenMP_7_taskGroup

namespace OpenMP_8_taskYield {
    void task() {
#pragma omp parallel
        {
#pragma omp single
            {
                for (int i = 0; i < 10; i++) {
#pragma omp task
                    {
                        printf("Task %d executed by thread %d\n", i,
                               omp_get_thread_num());
#pragma omp taskyield
                    }
                }
            }
        }
    }
}  // namespace OpenMP_8_taskYield

namespace OpenMP_9_lock {
    void task() {
        omp_lock_t lock;

        omp_init_lock(&lock);

#pragma omp parallel num_threads(4)
        {
            omp_set_lock(&lock);  // mutual exclusion (mutex)
            std::cout << "Thread " << omp_get_thread_num()
                      << " has acquired the lock. Sleeping 2 seconds..."
                      << std::endl;
            sleep(2);
            std::cout << "Thread " << omp_get_thread_num()
                      << " is releasing the lock..." << std::endl;
            omp_unset_lock(&lock);
        }
        omp_destroy_lock(&lock);
    }
}  // namespace OpenMP_9_lock

namespace OpenMP_10_flush {
    void task() {
#pragma omp parallel num_threads(8)
        {
#pragma omp critical
            std::cout << "Hello from thread " << omp_get_thread_num() << " of "
                      << omp_get_num_threads() << std::endl;
        }
    }
}  // namespace OpenMP_10_flush

int main() {
    OpenMP_1_critical::task();
    OpenMP_2_atomic::task();
    OpenMP_3_barrier::task();
    OpenMP_4_ordered::task();
    OpenMP_5_single::task();
    OpenMP_6_taskWait::task();
    OpenMP_7_taskGroup::task();
    OpenMP_8_taskYield::task();
    OpenMP_9_lock::task();
    OpenMP_10_flush::task();
    return 0;
}
