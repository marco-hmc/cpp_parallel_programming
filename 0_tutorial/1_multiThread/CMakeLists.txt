cmake_minimum_required(VERSION 3.16)
project(MultiThreadTutorial LANGUAGES CXX)

# C++17 标准
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# 查找依赖（可选）
find_package(OpenMP QUIET)
find_package(TBB QUIET)

# ============================================================
# 1_threadCreate - 线程创建
# ============================================================
add_executable(01_threadCreate
    1_threadCreate/1_threadCreate.cpp)
add_executable(02_threadManage
    1_threadCreate/2_threadManage.cpp)
add_executable(03_async
    1_threadCreate/3_async.cpp)
add_executable(04_packaged_task
    1_threadCreate/4_packaged_task.cpp)

# C++20: jthread
add_executable(05_jthread
    1_threadCreate/5_jthread.cpp)
target_compile_features(05_jthread PRIVATE cxx_std_20)

# C++17: parallel algorithms
add_executable(06_parallel_algorithms
    1_threadCreate/6_parallel_algorithms.cpp)

# ============================================================
# 2_threadsManage - 线程同步
# ============================================================
add_executable(07_mutex
    2_threadsManage/0_mutex.cpp)
add_executable(08_lockManager
    2_threadsManage/1_lockManager.cpp)
add_executable(09_wait
    2_threadsManage/2_wait.cpp)
add_executable(10_promise
    2_threadsManage/3_promise.cpp)
add_executable(11_atomic
    2_threadsManage/4_atomic_operator.cpp)
add_executable(12_memoryOrder
    2_threadsManage/5_memoryOrder.cpp)

# C++20: latch, barrier, semaphore
add_executable(13_cpp20_sync
    2_threadsManage/6_cpp20_sync.cpp)
target_compile_features(13_cpp20_sync PRIVATE cxx_std_20)

add_executable(14_deadlock
    2_threadsManage/7_deadlock.cpp)

# ============================================================
# 3_openmp
# ============================================================
if(OpenMP_FOUND)
    add_executable(15_openmp_control
        3_openmp/01_parallelControl.cpp)
    target_link_libraries(15_openmp_control PRIVATE OpenMP::OpenMP_CXX)

    add_executable(16_openmp_default
        3_openmp/02_default.cpp)
    target_link_libraries(16_openmp_default PRIVATE OpenMP::OpenMP_CXX)

    add_executable(17_openmp_sync
        3_openmp/03_parallelSync.cpp)
    target_link_libraries(17_openmp_sync PRIVATE OpenMP::OpenMP_CXX)

    add_executable(18_openmp_func
        3_openmp/04_func_and_env.cpp)
    target_link_libraries(18_openmp_func PRIVATE OpenMP::OpenMP_CXX)

    # Quiz
    add_executable(19_openmp_prod_consumer
        3_openmp/quiz/00_prodConsumer.cpp)
    target_link_libraries(19_openmp_prod_consumer PRIVATE OpenMP::OpenMP_CXX)

    add_executable(20_openmp_linked
        3_openmp/quiz/01_linked.cpp)
    target_link_libraries(20_openmp_linked PRIVATE OpenMP::OpenMP_CXX)

    add_executable(21_openmp_linked_tasks
        3_openmp/quiz/02_parallel_linked_list_tasks.cpp)
    target_link_libraries(21_openmp_linked_tasks PRIVATE OpenMP::OpenMP_CXX)

    add_executable(22_openmp_pi
        3_openmp/quiz/03_pi_calculate.cpp)
    target_link_libraries(22_openmp_pi PRIVATE OpenMP::OpenMP_CXX)

    add_executable(23_openmp_integration
        3_openmp/quiz/04_integration.cpp)
    target_link_libraries(23_openmp_integration PRIVATE OpenMP::OpenMP_CXX)
endif()

# ============================================================
# 4_tbb
# ============================================================
if(TBB_FOUND)
    add_executable(24_tbb_parallel_for
        4_tbb/0_parallel_for.cpp)
    target_link_libraries(24_tbb_parallel_for PRIVATE TBB::tbb)

    add_executable(25_tbb_exception
        4_tbb/1_exception.cpp)
    target_link_libraries(25_tbb_exception PRIVATE TBB::tbb)

    add_executable(26_tbb_containers
        4_tbb/2_concurrent_containers.cpp)
    target_link_libraries(26_tbb_containers PRIVATE TBB::tbb)

    add_executable(27_tbb_arena
        4_tbb/3_task_arena.cpp)
    target_link_libraries(27_tbb_arena PRIVATE TBB::tbb)

    add_executable(28_tbb_flow_graph
        4_tbb/4_flow_graph.cpp)
    target_link_libraries(28_tbb_flow_graph PRIVATE TBB::tbb)
endif()

# ============================================================
# 5_threadsQuiz
# ============================================================
add_executable(29_producer_consumer
    5_threadsQuiz/01_producerConsumer.cpp)
add_executable(30_print_order
    5_threadsQuiz/02_printOrder.cpp)
add_executable(31_threads_separate
    5_threadsQuiz/03_threads_separate.cpp)
add_executable(32_countdown_latch
    5_threadsQuiz/04_countDownLatch.cpp)
add_executable(33_pingpong_cache
    5_threadsQuiz/05_pingPongCache.cpp)
add_executable(34_atomic_vs_mutex
    5_threadsQuiz/06_AtomicVsMutex.cpp)
add_executable(35_readers_writers
    5_threadsQuiz/07_readersWriters.cpp)
add_executable(36_thread_pool
    5_threadsQuiz/08_threadPool.cpp)

# ============================================================
# 6_lockFree
# ============================================================
add_executable(37_cas
    6_lockFree/CAS.cpp)
add_executable(38_lockfree_queue
    6_lockFree/lockfree_queue.cpp)
add_executable(39_aba_problem
    6_lockFree/aba_problem.cpp)
