#include "workloads.h"

#include <cmath>
#include <random>

#include "utils.h"

namespace ppl_strategy {

// Deterministic initialization: fixed seed (42) guarantees identical access
// sequence and result every run — reproducible benchmarks.
const std::vector<double> kMemArray = [] {
    std::vector<double> data(kMemArrayElements);
    std::mt19937_64 gen(42);
    std::uniform_real_distribution<double> dis(0.0, 1.0);
    for (auto& v : data) v = dis(gen);
    return data;
}();

NO_OPTIMIZE double cpu_work(int start, int end) {
    double result = 0.0;
    for (int i = start; i < end; ++i) {
        result += std::sin(i * 0.001) * std::cos(i * 0.001) + std::sqrt(i + 1.0);
    }
    return result;
}

NO_OPTIMIZE double memory_work(int start, int end) {
    double sum = 0.0;
    // Knuth multiplicative hash — pseudo-random but deterministic access,
    // defeats hardware prefetcher → true DRAM latency/bandwidth bottleneck.
    constexpr std::size_t mask = kMemArrayElements - 1;
    constexpr std::size_t hash = 2654435761u;
    for (int i = start; i < end; ++i) {
        sum += kMemArray[(static_cast<std::size_t>(i) * hash) & mask];
    }
    return sum;
}

// ====== Uneven task distribution ======
// Heavy tasks are CONCENTRATED at indices 0..19 — this maximises the
// load-imbalance visibility for static partitioners (auto/affinity
// partitioners can mitigate it via work-stealing).

const std::vector<int> kUnevenTaskSizes = [] {
    // Tier 1: Heavy — 1% of tasks, 50% of total work (at the front)
    constexpr int kHeavyCount = 20;
    constexpr int kHeavySize = 100'000;  // 20 × 100K = 2.0M

    // Tier 2: Medium — 10% of tasks, 40% of total work
    constexpr int kMediumCount = 200;
    constexpr int kMediumSize = 8'000;  // 200 × 8K = 1.6M

    // Tier 3: Light — remaining ~89%, ~10% of total work
    constexpr int kLightCount =
        kUnevenNumTasks - kHeavyCount - kMediumCount;  // 1780
    constexpr int kLightTotal =
        kUnevenTotalElements - kHeavyCount * kHeavySize -
        kMediumCount * kMediumSize;  // 400,000
    constexpr int kLightSize = kLightTotal / kLightCount;  // 224

    std::vector<int> sizes;
    sizes.reserve(kUnevenNumTasks);
    for (int i = 0; i < kHeavyCount; ++i) sizes.push_back(kHeavySize);
    for (int i = 0; i < kMediumCount; ++i) sizes.push_back(kMediumSize);
    for (int i = 0; i < kLightCount; ++i) sizes.push_back(kLightSize);
    return sizes;
}();

// Shuffled variant: same task sizes, randomly permuted with fixed seed.
// This represents a more realistic distribution where heavy tasks are not
// clustered together.  Creates the clustered array first, then shuffles.
const std::vector<int> kUnevenTaskSizesShuffled = [] {
    // Copy the clustered sizes, then shuffle
    std::vector<int> sizes = kUnevenTaskSizes;
    std::mt19937_64 gen(42);  // fixed seed for reproducibility
    std::shuffle(sizes.begin(), sizes.end(), gen);
    return sizes;
}();

NO_OPTIMIZE double uneven_cpu_work(int task_idx) {
    int elements = kUnevenTaskSizes[static_cast<size_t>(task_idx)];
    return cpu_work(0, elements);
}

NO_OPTIMIZE double uneven_cpu_work_shuffled(int task_idx) {
    int elements = kUnevenTaskSizesShuffled[static_cast<size_t>(task_idx)];
    return cpu_work(0, elements);
}

}  // namespace ppl_strategy
