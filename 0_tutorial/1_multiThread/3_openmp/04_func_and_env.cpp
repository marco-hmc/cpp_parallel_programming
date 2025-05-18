#include <omp.h>

#include <iostream>

int main() {
    // 设置并行区域使用的线程数
    omp_set_num_threads(4);

    // 获取并行区域中可用的最大线程数
    int max_threads = omp_get_max_threads();
    std::cout << "Max threads: " << max_threads << std::endl;

    // 获取可用处理器的数量
    int num_procs = omp_get_num_procs();
    std::cout << "Number of processors: " << num_procs << std::endl;

    // 设置是否允许动态调整线程数
    omp_set_dynamic(1);

    // 获取当前动态调整线程数的设置
    int dynamic_threads = omp_get_dynamic();
    std::cout << "Dynamic threads enabled: " << dynamic_threads << std::endl;

// 并行区域
#pragma omp parallel
    {
        // 获取当前并行区域中的线程数
        int num_threads = omp_get_num_threads();
        // 获取当前线程的线程号
        int thread_num = omp_get_thread_num();
        // 判断当前是否在并行区域内
        int in_parallel = omp_in_parallel();

#pragma omp critical
        {
            std::cout << "Thread " << thread_num << " of " << num_threads
                      << " threads. In parallel region: " << in_parallel
                      << std::endl;
        }
    }

    return 0;
}