#include <omp.h>

#include <cstdio>
#include <iostream>

// ============================================================
namespace opm_default {
    void task() {
        int shared_var = 10;   // 默认共享变量
        int private_var = 20;  // 将被声明为私有变量

#pragma omp parallel default(shared) private(private_var)
        {
            private_var = omp_get_thread_num();  // 每个线程有自己的副本
            std::cout << "Thread " << omp_get_thread_num()
                      << " - shared_var: " << shared_var
                      << ", private_var: " << private_var << std::endl;
        }
    }
}  // namespace opm_default

// ============================================================
namespace opm_private {
    void task() {
        int shared_var = 100;  // 共享变量
        int private_var = -1;  // 私有变量

#pragma omp parallel private(private_var)
        {
            private_var = omp_get_thread_num();  // 每个线程有自己的副本
            std::cout << "Thread " << omp_get_thread_num()
                      << " - shared_var: " << shared_var
                      << ", private_var: " << private_var << std::endl;
        }

        std::cout << "After parallel region, private_var = " << private_var
                  << " (unchanged in main thread)" << std::endl;
    }
}  // namespace opm_private

// ============================================================
namespace opm_firstprivate {
    void task() {
        int i = 10;

#pragma omp parallel firstprivate(i)
        {
            // 'i' 被初始化为主线程的值
            printf("thread %d, i = %d\n", omp_get_thread_num(), i);
            i = 200 + omp_get_thread_num();
        }
        printf("After parallel region, i = %d\n", i);
    }
}  // namespace opm_firstprivate

// ============================================================
namespace opm_lastprivate {
    void task() {
        const int size = 1000;
        int i = -1, a[size];

#pragma omp parallel for lastprivate(i)
        for (i = 0; i < size; i++) a[i] = i;

        std::cout << "After parallel region, i = " << i << std::endl;
    }
}  // namespace opm_lastprivate

// ============================================================
namespace opm_reduction {
    static double two_body_energy(int i, int j) {
        return (2.0 * i + 3.0 * j) / 10.0;  // 模拟计算
    }

    void task() {
        const int nbodies = 1000;
        double energy = 0.0;

#pragma omp parallel for reduction(+ : energy)
        for (int i = 0; i < nbodies; i++) {
            for (int j = i + 1; j < nbodies; j++) {
                double eij = two_body_energy(i, j);
                energy += eij;
            }
        }

        std::cout << "Total energy = " << energy << std::endl;
    }
}  // namespace opm_reduction

// ============================================================

int main() {
    std::cout << "===== 1. default(shared) private =====\n";
    opm_default::task();

    std::cout << "\n===== 2. private =====\n";
    opm_private::task();

    std::cout << "\n===== 3. firstprivate =====\n";
    opm_firstprivate::task();

    std::cout << "\n===== 4. lastprivate =====\n";
    opm_lastprivate::task();

    std::cout << "\n===== 5. reduction =====\n";
    opm_reduction::task();
    return 0;
}
