#pragma once

#include <cstddef>
#include <vector>

namespace ppl_strategy {

// ====== Total work anchors ("same total work" invariant) ======
// CPU-bound: each element = sin*cos+sqrt; 4M elements ≈ 80–250ms serial
constexpr int kCpuElements = 4'000'000;
// Memory-bound: each element = 1 pseudo-random access into 128MB array;
// 8M ≈ similar serial time
constexpr int kMemElements = 8'000'000;

// ====== Granularity levels (chunk size = elements per task) ======
constexpr int kGrainUltraFine = 8;         // 4M/8   = 500K tasks (pure overhead)
constexpr int kGrainFine = 64;             // 4M/64  = 62.5K tasks
constexpr int kGrainMedium = 1'024;        // 4M/1K  = ~3.9K tasks
constexpr int kGrainCoarse = 16'384;       // 4M/16K = 244 tasks
constexpr int kGrainExtraCoarse = 65'536;  // 4M/64K = 61 tasks (~4 waves on 16T)

// ====== Nested parameters ======
// Three decomposition shapes, all same total = kCpuElements
constexpr int kNestDeepOuter = 5000;     // outer parts for deep nesting
constexpr int kNestDeepInner = 800;      // inner grain for deep nesting
constexpr int kNestMidOuter = 1000;      // outer parts for medium nesting
constexpr int kNestMidInner = 4000;      // inner grain for medium nesting
constexpr int kNestShallowOuter = 100;   // outer parts for shallow nesting
constexpr int kNestShallowInner = 40000; // inner grain for shallow nesting

// ====== Memory array (128 MB, >> L3 for cache-thrash effect) ======
constexpr std::size_t kMemArrayElements = 1u << 24;  // 16M doubles = 128 MB
extern const std::vector<double> kMemArray;  // init in workloads.cpp (fixed seed)

// ====== Uneven task distribution for load-imbalance benchmarks ======
// Task sizes vary ~450× from lightest to heaviest; heavy tasks clustered at
// indices 0..19 to create a worst-case load-imbalance pattern for static
// partitioners (auto/affinity partitioners handle this via work-stealing).
//
//   Tier 1 (heavy):  20 tasks × 100K elements = 2.0M  (50% of total)
//   Tier 2 (medium): 200 tasks ×   8K elements = 1.6M  (40%)
//   Tier 3 (light): 1780 tasks ×  ~225 elements = 0.4M  (10%)
//
// Total: 2000 tasks, ~4M elements (≈ kCpuElements for fair comparison)
constexpr int kUnevenNumTasks = 2000;
constexpr int kUnevenTotalElements = 4'000'000;
extern const std::vector<int> kUnevenTaskSizes;  // init in workloads.cpp
extern const std::vector<int> kUnevenTaskSizesShuffled;  // same sizes, shuffled

double uneven_cpu_work(int task_idx);  // cpu_work on kUnevenTaskSizes[task_idx] elements
double uneven_cpu_work_shuffled(int task_idx);  // cpu_work on kUnevenTaskSizesShuffled[task_idx]

// Kernels: both take (start, end) element-range, return double
double cpu_work(int start, int end);
double memory_work(int start, int end);

}  // namespace ppl_strategy
