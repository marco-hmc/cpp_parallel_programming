#pragma once
// Eight Queens — algorithm implementations extracted from standalone programs
// and adapted for Google Benchmark.
//
// All solvers are deterministic: same N → same count, same work.
// Multi-threaded variants use top-level first-row split with mirror symmetry.

#include <cassert>
#include <cstdint>
#include <cstring>
#include <thread>
#include <vector>

// ---- Portability: count trailing zeros ----
#if defined(__GNUC__) || defined(__clang__)
#define NQ_CTZ __builtin_ctz
#elif defined(_MSC_VER)
#include <intrin.h>
inline int NQ_CTZ(unsigned int x) {
    unsigned long idx;
    _BitScanForward(&idx, x);
    return static_cast<int>(idx);
}
#else
inline int NQ_CTZ(unsigned int x) {
    int n = 0;
    while ((x & 1) == 0) { x >>= 1; ++n; }
    return n;
}
#endif

namespace nqueens {

// =========================================================================
// 1. Classic backtracking — bool arrays for column/diagonal marking
//    Complexity: O(N!) with pruning.  kMaxQueens = 20.
// =========================================================================

inline int64_t solve_backtracking_serial(int N) {
    static constexpr int kMax = 20;
    assert(0 < N && N <= kMax);

    bool columns[kMax]{};
    bool diag[2 * kMax]{};
    bool antidiag[2 * kMax]{};
    int64_t count = 0;

    // Recursive lambda via std::function would add overhead;
    // use a raw function pointer with capture via void*.
    struct State { int N; int64_t* count; bool* columns; bool* diag; bool* antidiag; };

    // We use a manual stack to avoid std::function overhead in the hot path.
    // Each stack frame: (row, col_index)
    struct Frame { int row; int col; };
    Frame stack[kMax];
    int sp = 0;

    // Iterative backtracking with explicit stack
    int row = 0;
    int col = 0;

    while (true) {
        if (row == N) {
            ++count;
            // backtrack
            if (sp == 0) break;
            --sp;
            row = stack[sp].row;
            col = stack[sp].col;
            // undo placement at (row, col)
            int d = N + row - col;
            columns[col] = false;
            antidiag[row + col] = false;
            diag[d] = false;
            ++col;  // try next column
            continue;
        }

        // Find next valid column
        while (col < N) {
            int d = N + row - col;
            if (!(columns[col] || antidiag[row + col] || diag[d])) {
                // Place queen
                columns[col] = true;
                antidiag[row + col] = true;
                diag[d] = true;
                stack[sp++] = {row, col};
                row++;
                col = 0;
                break;
            }
            ++col;
        }

        if (col >= N) {
            // No valid column — backtrack
            if (sp == 0) break;
            --sp;
            row = stack[sp].row;
            col = stack[sp].col;
            // undo
            int d = N + row - col;
            columns[col] = false;
            antidiag[row + col] = false;
            diag[d] = false;
            ++col;
        }
    }

    return count;
}

// =========================================================================
// 2. Dancing Links (Algorithm X) — Knuth's exact cover
//    kMaxQueens = 20.  Fixed-size node pool allocation.
// =========================================================================

namespace dl {

constexpr int kMaxQueens = 20;
constexpr int kMaxColumns = kMaxQueens * 6;
constexpr int kMaxNodes = 1 + kMaxColumns + kMaxQueens * kMaxQueens * 4;

struct Node;
using Column = Node;
struct Node {
    Node* left;
    Node* right;
    Node* up;
    Node* down;
    Column* col;
    int name;
    int size;
};

class Solver {
public:
    explicit Solver(int N) {
        assert(N <= kMaxQueens);
        (void)N;  // used only in assert
        root_ = &nodes_[cur_++];
        root_->left = root_->right = root_;
        root_->up = root_->down = root_;
        root_->col = root_;
        root_->name = -1;
        root_->size = 0;

        // Primary columns: each column must have exactly one queen → N
        //                    each row must have exactly one queen → N
        for (int i = 0; i < 2 * N; ++i) {
            Column* c = new_col(i);
            put_left(root_, c);
            cols_[i] = c;
        }
        // Secondary columns: each diagonal may have 0 or 1 queen → 4N
        for (int j = 0; j < 4 * N; ++j) {
            Column* c = new_col(2 * N + j);
            cols_[2 * N + j] = c;
        }

        for (int r = 0; r < N; ++r) {
            for (int c = 0; c < N; ++c) {
                Node* n0 = new_row(c);              // column constraint
                Node* n1 = new_row(N + r);           // row constraint
                Node* n2 = new_row(2 * N + r + c);  // main diagonal
                Node* n3 = new_row(5 * N + r - c);  // anti diagonal
                put_left(n0, n1);
                put_left(n0, n2);
                put_left(n0, n3);
            }
        }
    }

    int64_t solve() {
        count_ = 0;
        solve_impl();
        return count_;
    }

private:
    int64_t count_ = 0;
    int cur_ = 0;
    Node nodes_[kMaxNodes]{};
    Column* cols_[kMaxColumns]{};
    Column* root_;

    Column* new_col(int name) {
        Column* c = &nodes_[cur_++];
        c->left = c->right = c->up = c->down = c;
        c->col = c;
        c->name = name;
        c->size = 0;
        return c;
    }

    Node* new_row(int col_idx) {
        Node* r = &nodes_[cur_++];
        r->left = r->right = r->up = r->down = r;
        r->name = col_idx;
        r->col = cols_[col_idx];
        Column* c = r->col;
        r->up = c->up;
        r->down = c;
        c->up->down = r;
        c->up = r;
        c->size++;
        r->col = c;
        return r;
    }

    Column* get_min_column() {
        Column* best = root_->right;
        int min_sz = best->size;
        if (min_sz > 1) {
            for (Column* c = best->right; c != root_; c = c->right) {
                if (c->size < min_sz) {
                    best = c;
                    min_sz = c->size;
                    if (min_sz <= 1) break;
                }
            }
        }
        return best;
    }

    void cover(Column* c) {
        c->right->left = c->left;
        c->left->right = c->right;
        for (Node* row = c->down; row != c; row = row->down) {
            for (Node* j = row->right; j != row; j = j->right) {
                j->down->up = j->up;
                j->up->down = j->down;
                j->col->size--;
            }
        }
    }

    void uncover(Column* c) {
        for (Node* row = c->up; row != c; row = row->up) {
            for (Node* j = row->left; j != row; j = j->left) {
                j->col->size++;
                j->down->up = j;
                j->up->down = j;
            }
        }
        c->right->left = c;
        c->left->right = c;
    }

    void put_left(Column* old, Column* nnew) {
        nnew->left = old->left;
        nnew->right = old;
        old->left->right = nnew;
        old->left = nnew;
    }

    void solve_impl() {
        if (root_->right == root_) {
            ++count_;
            return;
        }
        Column* col = get_min_column();
        cover(col);
        for (Node* row = col->down; row != col; row = row->down) {
            for (Node* j = row->right; j != row; j = j->right) cover(j->col);
            solve_impl();
            for (Node* j = row->left; j != row; j = j->left) uncover(j->col);
        }
        uncover(col);
    }
};

}  // namespace dl

inline int64_t solve_dancing_links(int N) {
    dl::Solver solver(N);
    return solver.solve();
}

// =========================================================================
// 3. Bit-optimized backtracking — uint32_t masks, __builtin_ctz
//    Uses running shift registers for diagonals (no array indexing).
//    kMaxQueens = 20 for uint32_t (actually N <= 16 safe for int64_t count).
// =========================================================================

inline int64_t solve_bitopt_serial(int N) {
    assert(0 < N && N <= 20);
    const uint32_t mask = ~((1u << N) - 1);  // bits ≥ N are 1 → "occupied"
    int64_t count = 0;

    // Iterative DFS with explicit stack
    struct Frame { int row; uint32_t cols; uint32_t diag; uint32_t anti; int i; };
    // Max depth = N, but the stack is small
    Frame stack[20];
    int sp = 0;

    uint32_t cols = 0, diag = 0, anti = 0;
    int row = 0;
    uint32_t avail = ~(cols | diag | anti | mask);

    while (true) {
        if (avail == 0) {
            // No available positions — backtrack
            if (sp == 0) break;
            --sp;
            row = stack[sp].row;
            cols = stack[sp].cols;
            diag = stack[sp].diag;
            anti = stack[sp].anti;
            int i = stack[sp].i;
            uint32_t m = 1u << i;
            // Advance past this column
            uint32_t before = ~(cols | diag | anti | mask);
            uint32_t mask_i = before & (m - 1);  // bits before i
            avail = before & ~(mask_i | m);       // bits after i
            continue;
        }

        int i = NQ_CTZ(avail);
        uint32_t m = 1u << i;

        if (row == N - 1) {
            ++count;
            avail &= avail - 1;  // clear this bit, try next
        } else {
            stack[sp++] = {row, cols, diag, anti, i};
            cols = cols | m;
            diag = (diag | m) >> 1;
            anti = (anti | m) << 1;
            row++;
            avail = ~(cols | diag | anti | mask);
        }
    }

    return count;
}

// =========================================================================
// 4. Bit-optimized sub-problem: count solutions with first-row queen at col=i
// =========================================================================

inline int64_t solve_bitopt_sub(int N, int col0) {
    assert(0 < N && N <= 20);
    assert(0 <= col0 && col0 < N);
    const uint32_t mask = ~((1u << N) - 1);
    int64_t count = 0;

    struct Frame { int row; uint32_t cols; uint32_t diag; uint32_t anti; int i; };
    Frame stack[20];
    int sp = 0;

    uint32_t m0 = 1u << col0;
    uint32_t cols = m0, diag = m0 >> 1, anti = m0 << 1;
    int row = 1;
    uint32_t avail = ~(cols | diag | anti | mask);

    while (true) {
        if (avail == 0) {
            if (sp == 0) break;
            --sp;
            row = stack[sp].row;
            cols = stack[sp].cols;
            diag = stack[sp].diag;
            anti = stack[sp].anti;
            int i = stack[sp].i;
            uint32_t m = 1u << i;
            uint32_t before = ~(cols | diag | anti | mask);
            avail = before & ~((before & (m - 1)) | m);
            continue;
        }
        int i = NQ_CTZ(avail);
        uint32_t m = 1u << i;
        if (row == N - 1) {
            ++count;
            avail &= avail - 1;
        } else {
            stack[sp++] = {row, cols, diag, anti, i};
            cols = cols | m;
            diag = (diag | m) >> 1;
            anti = (anti | m) << 1;
            row++;
            avail = ~(cols | diag | anti | mask);
        }
    }
    return count;
}

// =========================================================================
// 5. Multi-threaded: top-level first-row split + mirror symmetry
//    Works for BOTH backtracking and bit-optimized backends.
// =========================================================================

using SubSolver = int64_t (*)(int N, int col0);

// Backtracking sub-solver (used by MT backtracking variant)
inline int64_t solve_backtracking_sub(int N, int col0) {
    static constexpr int kMax = 20;
    assert(0 < N && N <= kMax);

    bool columns[kMax]{};
    bool diag[2 * kMax]{};
    bool antidiag[2 * kMax]{};
    int64_t count = 0;

    // Place first queen at (0, col0)
    columns[col0] = true;
    antidiag[0 + col0] = true;
    diag[N + 0 - col0] = true;

    // Iterative backtracking from row 1
    struct Frame { int row; int col; };
    Frame stack[kMax];
    int sp = 0;
    int row = 1;
    int col = 0;

    while (true) {
        if (row == N) {
            ++count;
            if (sp == 0) break;
            --sp;
            row = stack[sp].row;
            col = stack[sp].col;
            int d = N + row - col;
            columns[col] = false;
            antidiag[row + col] = false;
            diag[d] = false;
            ++col;
            continue;
        }
        while (col < N) {
            int d = N + row - col;
            if (!(columns[col] || antidiag[row + col] || diag[d])) {
                columns[col] = true;
                antidiag[row + col] = true;
                diag[d] = true;
                stack[sp++] = {row, col};
                row++;
                col = 0;
                break;
            }
            ++col;
        }
        if (col >= N) {
            if (sp == 0) break;
            --sp;
            row = stack[sp].row;
            col = stack[sp].col;
            int d = N + row - col;
            columns[col] = false;
            antidiag[row + col] = false;
            diag[d] = false;
            ++col;
        }
    }
    return count;
}

// Core MT runner: split first row into (N+1)/2 sub-problems,
// solve each with symmetry ×2 (except middle column if N odd).
inline int64_t solve_mt(int N, SubSolver sub_solver, int num_threads,
                        bool use_symmetry) {
    if (num_threads <= 0)
        num_threads = static_cast<int>(std::thread::hardware_concurrency());

    int total_subs;
    if (use_symmetry) {
        total_subs = (N + 1) / 2;  // mirror symmetry: only need half
    } else {
        total_subs = N;  // full board: every first-row column
    }

    std::vector<std::thread> threads;
    threads.reserve(total_subs);

    struct Result {
        int col;
        int64_t count;
    };
    std::vector<Result> results(total_subs);

    // Assign sub-problems to threads
    int subs_per_thread = total_subs / num_threads;
    int remainder = total_subs % num_threads;
    int start = 0;

    for (int t = 0; t < num_threads && start < total_subs; ++t) {
        int count = subs_per_thread + (t < remainder ? 1 : 0);
        if (count <= 0) continue;
        int end = start + count;
        threads.emplace_back([&results, sub_solver, N, start, end]() {
            for (int idx = start; idx < end; ++idx) {
                results[idx].col = idx;
                results[idx].count = sub_solver(N, idx);
            }
        });
        start = end;
    }

    for (auto& t : threads) t.join();

    int64_t total = 0;
    if (use_symmetry) {
        for (auto& r : results) {
            if (N % 2 == 1 && r.col == N / 2) {
                total += r.count;          // middle column: no symmetry double
            } else {
                total += 2 * r.count;      // mirror double-count
            }
        }
    } else {
        for (auto& r : results) total += r.count;
    }

    return total;
}

// ---- Public API wrappers ----

inline int64_t solve_backtracking_mt(int N, int num_threads = 0) {
    return solve_mt(N, solve_backtracking_sub, num_threads, true);
}

inline int64_t solve_bitopt_mt(int N, int num_threads = 0) {
    return solve_mt(N, solve_bitopt_sub, num_threads, true);
}

// For symmetry-benefit measurement: full board without mirror optimization
inline int64_t solve_bitopt_mt_no_symmetry(int N, int num_threads = 0) {
    return solve_mt(N, solve_bitopt_sub, num_threads, false);
}

}  // namespace nqueens
