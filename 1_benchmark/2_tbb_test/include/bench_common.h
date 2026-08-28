#pragma once
// 2_tbb_test 共享工具。
//
// 全局政策（v2 受控实验，与 CONTEXT.md / 1_threadPool_test v2 一致）：
// - 禁用 std::random_device：一切随机性必须用固定种子，保证全策略每迭代复用同一负载；
// - 不用 assert（Release 下是空操作）：校验走 verifyResult / verifyExact（stderr + abort）；
// - 槽位归约：每个 chunk 把部分和写入自己的槽位，主线程按槽位升序合并——
//   归约顺序 = 元素顺序，同时消灭了旧版共享 atomic 的争用。注意：不同策略的
//   累加分组不同（chunk 部分和 vs 单一连续累加），浮点结果存在 ULP 级差异
//   （实测相对误差 ≈1e-14），故浮点校验用相对容差 1e-9（真实缺陷信号 ≥1e-3）；
// - 负载函数标 NO_OPTIMIZE：保证任何调用点生成同一份代码，且各策略每元素成本一致。

#include <oneapi/tbb.h>

#include <algorithm>
#include <barrier>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <map>
#include <random>
#include <thread>
#include <tuple>
#include <vector>

#include "utils.h"

// ---------------------------------------------------------------------------
// 校验
// ---------------------------------------------------------------------------

// tol == 0 时要求逐位相等（槽位归约下可达）；否则按相对误差校验。
inline void verifyResult(const char* name, double got, double ref, double tol) {
    const double diff = std::fabs(got - ref);
    const double scale = std::max({std::fabs(got), std::fabs(ref), 1.0});
    if (diff > tol * scale) {
        std::fprintf(stderr,
                     "VERIFY FAILED [%s]: got=%.17g ref=%.17g diff=%.17g\n",
                     name, got, ref, diff);
        std::abort();
    }
}

template <typename T>
inline void verifyExact(const char* name, T got, T ref) {
    if (got != ref) {
        std::fprintf(stderr, "VERIFY FAILED [%s]: got=%lld ref=%lld\n", name,
                     static_cast<long long>(got), static_cast<long long>(ref));
        std::abort();
    }
}

// 校验宏：以所在函数名（__func__）作为失败标识，避免逐行硬编码名称。
#define VERIFY_RESULT(got, ref, tol) verifyResult(__func__, (got), (ref), (tol))
#define VERIFY_EXACT(got, ref) verifyExact(__func__, (got), (ref))

// 槽位归约：把 [begin, end) 按规范 chunk 边界（grain 对齐）拆段，逐段计算部分和
// 并原子累加进槽位。要点（实测教训）：
// - static/affinity 分区器的叶子起点不保证 grain 对齐（等分负载优先），一个规范
//   chunk 可能被相邻两个叶子各写一部分 → 槽位必须是 atomic 的 fetch_add，
//   不能是赋值（赋值会丢失先写叶子的部分和，实测丢 0.2%~2.7%）；
// - 槽位间无共享（grain=1 时百万独立槽位），争用可忽略；边界共享槽位的累加
//   顺序不确定 → ULP 级噪声（≤1e-14），在 1e-9 校验容差内。
inline double slotMerge(const std::vector<std::atomic<double>>& sums) {
    double total = 0.0;
    for (const auto& s : sums) {
        total += s.load(std::memory_order_relaxed);
    }
    return total;
}

template <typename Compute>  // Compute(double& acc, int b, int e)：累加 [b,e) 的部分和
inline void slotAccumulate(std::vector<std::atomic<double>>& sums, int begin,
                           int end, int grain, int slot_offset,
                           Compute&& compute) {
    int b = begin;
    while (b < end) {
        const int e = std::min(b + (grain - b % grain), end);
        double local = 0.0;
        compute(local, b, e);
        sums[slot_offset + b / grain].fetch_add(local, std::memory_order_relaxed);
        b = e;
    }
}

// ---------------------------------------------------------------------------
// case1 负载：阶梯偏斜的每元素成本
// ---------------------------------------------------------------------------

// 元素总数：2^20 = 1048576，可被粒度 {1, 256, 4096, 65536} 整除，
// 保证 chunk 数与槽位数精确相等（case2 同理由）。
constexpr int TOTAL_WORK = 1 << 20;

// 标定旋钮：每元素执行的 sin/cos/sqrt 三元组个数。
// 标定目标：amp=0 串行 ≈ 20~30ms（实测后调整）。
constexpr int BASE_UNITS = 1;

// 4 段阶梯偏斜：quarter = (i*4)/TOTAL_WORK ∈ [0,3]，权重 = 1 + amp*quarter。
// amp=0 完全均匀；amp=8 时第 4 段成本是第 1 段的 25 倍。
// 纯函数：同一 i 在所有策略中得到相同值。
NO_OPTIMIZE inline double elemCost(int i, int amp) {
    const int quarter = static_cast<int>((static_cast<long long>(i) * 4) / TOTAL_WORK);
    const int units = BASE_UNITS * (1 + amp * quarter);
    double acc = 0.0;
    for (int u = 0; u < units; ++u) {
        const double x = static_cast<double>(i) + u * 0.125;
        acc += std::sin(x) * std::cos(x) + std::sqrt(static_cast<double>(i + u + 1));
    }
    return acc;
}

// 串行参考值：每个 (amp) 只算一次（计时窗外），全部策略与之比对。
inline double case1Reference(int amp) {
    static double ref[16] = {};
    static bool done[16] = {};
    if (!done[amp]) {
        double sum = 0.0;
        for (int i = 0; i < TOTAL_WORK; ++i) {
            sum += elemCost(i, amp);
        }
        ref[amp] = sum;
        done[amp] = true;
    }
    return ref[amp];
}

// ---------------------------------------------------------------------------
// case2 工具：精确锁次数配对 + 持久线程组控制组
// ---------------------------------------------------------------------------

// 所有 TBB 行限定在 8 线程 arena 内执行，与 PersistentTeam(8) 配对。
// 注意：锁次数配对不能依赖分区器叶子行为——static_partitioner 按线程数
// 等分（2^20 范围只有 ~12 个叶子），锁次数必须由 batch 索引域构造保证
// （见 case2 的 tbbBatchFor8）。
inline tbb::task_arena& arena8() {
    static tbb::task_arena arena(8);
    return arena;
}

// 持久线程组控制组：与 TBB 常驻 worker 池同生命周期（构造在计时窗外），
// 用于把「线程生命周期」从「锁质量」中剥离出来。
// std::barrier 双栅栏（出发/会合），run(fn) 并发执行 fn(worker_id) 并等待全部完成。
class PersistentTeam {
public:
    explicit PersistentTeam(int n)
        : start_(n + 1), end_(n + 1), stop_(false) {
        for (int t = 0; t < n; ++t) {
            workers_.emplace_back([this, t]() {
                while (true) {
                    start_.arrive_and_wait();
                    if (stop_) {
                        return;
                    }
                    fn_(t);
                    end_.arrive_and_wait();
                }
            });
        }
    }

    ~PersistentTeam() {
        stop_ = true;
        start_.arrive_and_wait();
        for (auto& w : workers_) {
            w.join();
        }
    }

    void run(const std::function<void(int)>& fn) {
        fn_ = fn;  // 写发生在 start_ 到达之前，worker 越过栅栏后可见
        start_.arrive_and_wait();
        end_.arrive_and_wait();
    }

    PersistentTeam(const PersistentTeam&) = delete;
    PersistentTeam& operator=(const PersistentTeam&) = delete;

private:
    std::barrier<> start_;
    std::barrier<> end_;
    bool stop_;
    std::function<void(int)> fn_;
    std::vector<std::thread> workers_;
};

// ---------------------------------------------------------------------------
// case3 工具：确定性嵌套负载
// ---------------------------------------------------------------------------

// 纯函数、正值（同号累加 → 浮点重排误差有界，槽位归约下则逐位一致）。
NO_OPTIMIZE inline double case3ElemOp(int i, int j) {
    const double x = static_cast<double>(i + 1) * 100000.0 + static_cast<double>(j + 1);
    return std::sqrt(x) * std::log(x + 2.0) + 0.001 * std::pow(x, 1.1);
}

struct Case3Spec {
    int outer = 0;
    int g = 0;                       // 内层 chunk 粒度
    std::vector<int> inner_sizes;    // 每个外层任务的元素数
    std::vector<int> chunk_offsets;  // 每个外层任务在槽位数组中的首槽位
    std::vector<double> reference;   // 每个外层任务的串行参考和
    int total_chunks = 0;

    // 均匀：全部任务 = mean_inner；偏斜：伪随机 [mean_inner/4, 4*mean_inner]。
    // 固定种子：全策略每迭代复用同一负载（旧版 random_device 无种子的缺陷修正）。
    static Case3Spec make(int outer, int mean_inner, bool skewed, int g,
                          uint32_t seed = 42) {
        Case3Spec spec;
        spec.outer = outer;
        spec.g = g;

        std::mt19937 gen(seed);
        spec.inner_sizes.reserve(outer);
        const int lo = skewed ? mean_inner / 4 : mean_inner;
        const int hi = skewed ? mean_inner * 4 : mean_inner;
        for (int i = 0; i < outer; ++i) {
            spec.inner_sizes.push_back(
                lo + (skewed ? static_cast<int>(gen() % (hi - lo + 1)) : 0));
        }

        spec.chunk_offsets.reserve(outer + 1);
        spec.chunk_offsets.push_back(0);
        for (int s : spec.inner_sizes) {
            spec.chunk_offsets.push_back(spec.chunk_offsets.back() + (s + g - 1) / g);
        }
        spec.total_chunks = spec.chunk_offsets.back();

        spec.reference.reserve(outer);
        for (int i = 0; i < outer; ++i) {
            double acc = 0.0;
            for (int j = 0; j < spec.inner_sizes[i]; ++j) {
                acc += case3ElemOp(i, j);
            }
            spec.reference.push_back(acc);
        }
        return spec;
    }

    double totalReference() const {
        double sum = 0.0;
        for (double r : reference) {
            sum += r;
        }
        return sum;
    }

    int maxInner() const {
        int m = 0;
        for (int s : inner_sizes) {
            m = std::max(m, s);
        }
        return m;
    }
};

// 按 (outer, mean_inner, skewed, g) 惰性缓存 spec；基准注册前单线程初始化，无锁安全。
inline const Case3Spec& case3Spec(int outer, int mean_inner, bool skewed, int g) {
    using Key = std::tuple<int, int, bool, int>;
    static std::map<Key, Case3Spec> cache;
    const Key key{outer, mean_inner, skewed, g};
    auto it = cache.find(key);
    if (it == cache.end()) {
        it = cache.emplace(key, Case3Spec::make(outer, mean_inner, skewed, g)).first;
    }
    return it->second;
}
