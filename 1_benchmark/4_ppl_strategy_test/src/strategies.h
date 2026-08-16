#pragma once

#include <ppl.h>

#include <atomic>
#include <future>
#include <thread>
#include <vector>

#include "threadPool.h"
#include "workloads.h"

namespace ppl_strategy {

using WorkKernel = double (*)(int, int);

// ======================================================================
// 0. Serial baseline — the reference for all speedup calculations
// ======================================================================
inline double run_serial(WorkKernel kernel, int total) {
    return kernel(0, total);
}

// ======================================================================
// 1. PPL parallel_for, default (auto_partitioner)
//    PPL's built-in default: adaptive chunking with dynamic load balance.
//    Each iteration processes one element; the partitioner groups them.
//    Equivalent to TBB's auto_partitioner.
// ======================================================================
inline double run_ppl_auto(WorkKernel kernel, int total) {
    std::atomic<double> sum{0.0};
    Concurrency::parallel_for(0, total, [&](int i) {
        double local = kernel(i, i + 1);
        sum.fetch_add(local, std::memory_order_relaxed);
    });
    return sum.load();
}

// ======================================================================
// 2. PPL parallel_for + static_partitioner
//    Divides range into ~num_threads equal slabs, no work-stealing.
//    Coarsest possible grain (one chunk per thread).
// ======================================================================
inline double run_ppl_static(WorkKernel kernel, int total) {
    std::atomic<double> sum{0.0};
    Concurrency::parallel_for(0, total,
                              [&](int i) {
                                  double local = kernel(i, i + 1);
                                  sum.fetch_add(local, std::memory_order_relaxed);
                              },
                              Concurrency::static_partitioner());
    return sum.load();
}

// ======================================================================
// 3. PPL parallel_for + affinity_partitioner
//    Remembers range→thread mapping across calls for better cache/NUMA
//    affinity on repeated runs. Static local persists across benchmark
//    iterations — the correct usage pattern.
// ======================================================================
inline double run_ppl_affinity(WorkKernel kernel, int total) {
    static Concurrency::affinity_partitioner ap;
    std::atomic<double> sum{0.0};
    Concurrency::parallel_for(0, total,
                              [&](int i) {
                                  double local = kernel(i, i + 1);
                                  sum.fetch_add(local, std::memory_order_relaxed);
                              },
                              ap);
    return sum.load();
}

// ======================================================================
// 5. Manual chunking: pre-split by grain, then parallel_for over chunks.
//    This is HOW PPL achieves arbitrary grain-size control — there's no
//    blocked_range equivalent. Each parallel_for iteration calls kernel
//    on grain elements sequentially.
//    Primary mechanism for granularity sweeps in case1/case2.
// ======================================================================
inline double run_ppl_chunked(WorkKernel kernel, int total, int grain) {
    std::atomic<double> sum{0.0};
    int num_chunks = (total + grain - 1) / grain;
    Concurrency::parallel_for(0, num_chunks, [&](int c) {
        int start = c * grain;
        int end = std::min(start + grain, total);
        sum.fetch_add(kernel(start, end), std::memory_order_relaxed);
    });
    return sum.load();
}

// ======================================================================
// 6. Nested parallel_for
//    Outer: parallel_for over outer_parts chunks.
//    Inner: each outer body spawns its own parallel_for to subdivide.
//    PPL scheduler handles nesting natively — no deadlock risk.
// ======================================================================
inline double run_ppl_nested(WorkKernel kernel, int total, int outer_parts,
                              int inner_grain) {
    std::atomic<double> sum{0.0};
    int outer_size = (total + outer_parts - 1) / outer_parts;
    Concurrency::parallel_for(0, outer_parts, [&](int o) {
        int start = o * outer_size;
        int end = std::min(start + outer_size, total);
        std::atomic<double> inner_sum{0.0};
        int num_inner = (end - start + inner_grain - 1) / inner_grain;
        Concurrency::parallel_for(0, num_inner, [&](int c) {
            int is = start + c * inner_grain;
            int ie = std::min(is + inner_grain, end);
            inner_sum.fetch_add(kernel(is, ie), std::memory_order_relaxed);
        });
        sum.fetch_add(inner_sum.load(), std::memory_order_relaxed);
    });
    return sum.load();
}

// ======================================================================
// 7. Outer-only parallel_for (inner serial)
//    Parallelize only the outer loop; each outer task runs its inner
//    work serially. Simplest "nested" strategy, useful for load-imbalance
//    measurement.
// ======================================================================
inline double run_ppl_outer_only(WorkKernel kernel, int total,
                                  int outer_parts) {
    std::atomic<double> sum{0.0};
    int outer_size = (total + outer_parts - 1) / outer_parts;
    Concurrency::parallel_for(0, outer_parts, [&](int o) {
        int start = o * outer_size;
        int end = std::min(start + outer_size, total);
        sum.fetch_add(kernel(start, end), std::memory_order_relaxed);
    });
    return sum.load();
}

// ======================================================================
// 8. PPL task_group: pre-split into explicit tasks, group.wait()
//    Explicit task management — each chunk is a separate task run().
// ======================================================================
inline double run_ppl_task_group(WorkKernel kernel, int total, int grain) {
    std::atomic<double> sum{0.0};
    Concurrency::task_group tg;
    for (int start = 0; start < total; start += grain) {
        int end = std::min(start + grain, total);
        tg.run([&, kernel, start, end] {
            sum.fetch_add(kernel(start, end), std::memory_order_relaxed);
        });
    }
    tg.wait();
    return sum.load();
}

// ======================================================================
// 9. Nested task_group (outer + inner)
//    Outer tasks submitted as task_group runs; inner work also task_group.
// ======================================================================
inline double run_ppl_task_group_nested(WorkKernel kernel, int total,
                                         int outer_parts, int inner_grain) {
    std::atomic<double> sum{0.0};
    Concurrency::task_group outer_tg;
    int outer_size = (total + outer_parts - 1) / outer_parts;
    for (int o = 0; o < outer_parts; ++o) {
        int start = o * outer_size;
        int end = std::min(start + outer_size, total);
        outer_tg.run([&, kernel, start, end, inner_grain] {
            Concurrency::task_group inner_tg;
            std::atomic<double> inner_sum{0.0};
            for (int is = start; is < end; is += inner_grain) {
                int ie = std::min(is + inner_grain, end);
                inner_tg.run([&, kernel, is, ie] {
                    inner_sum.fetch_add(kernel(is, ie),
                                        std::memory_order_relaxed);
                });
            }
            inner_tg.wait();
            sum.fetch_add(inner_sum.load(), std::memory_order_relaxed);
        });
    }
    outer_tg.wait();
    return sum.load();
}

// ======================================================================
// 10. Manual partition → StdThreadPool
//     Pre-split work into N equal chunks, submit to fixed thread pool.
//     No work-stealing; purely static load balance.
// ======================================================================
inline double run_pool(WorkKernel kernel, int total, int partitions) {
    static StdThreadPool::ThreadPool pool(
        std::thread::hardware_concurrency());
    std::vector<std::future<double>> futures;
    futures.reserve(static_cast<std::size_t>(partitions));
    int chunk = total / partitions;
    for (int p = 0; p < partitions; ++p) {
        int start = p * chunk;
        int end = (p == partitions - 1) ? total : start + chunk;
        futures.push_back(
            pool.submitTask([kernel, start, end] { return kernel(start, end); }));
    }
    double sum = 0.0;
    for (auto& f : futures) sum += f.get();
    return sum;
}

}  // namespace ppl_strategy
