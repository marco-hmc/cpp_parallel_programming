#include <algorithm>
#include <cassert>
#include <chrono>
#include <iostream>
#include <numeric>
#include <vector>

double now() {
    using namespace std::chrono;
    return duration_cast<duration<double>>(
               steady_clock::now().time_since_epoch())
        .count();
}

int bruteforce(const int N) {
    int count = 0;
    std::vector<int> pos(N);
    std::iota(pos.begin(), pos.end(), 0);
    do {
        std::vector<bool> diagnoal(2 * N, false);
        std::vector<bool> antidiagnoal(2 * N, false);

        for (int i = 0; i < N; ++i) {
            if (antidiagnoal[i + pos[i]]) {
                goto infeasible;
            } else {
                antidiagnoal[i + pos[i]] = true;
            }
        }

        for (int i = 0; i < N; ++i) {
            const int d = N + i - pos[i];
            if (diagnoal[d]) {
                goto infeasible;
            } else {
                diagnoal[d] = true;
            }
        }

        ++count;
    infeasible:;
    } while (std::next_permutation(pos.begin(), pos.end()));
    return count;
}

struct BackTracking {
    static constexpr int kMaxQueens = 16;
    const int N;
    int count;
    std::vector<bool> columns;
    std::vector<bool> diagnoal;
    std::vector<bool> antidiagnoal;

    BackTracking(int nqueens)
        : N(nqueens),
          count(0),
          columns(kMaxQueens, false),
          diagnoal(2 * kMaxQueens, false),
          antidiagnoal(2 * kMaxQueens, false) {
        assert(0 < N && N <= kMaxQueens);
    }

    void search(const int row) {
        for (int i = 0; i < N; ++i) {
            const int d = N + row - i;
            if (!(columns[i] || antidiagnoal[row + i] || diagnoal[d])) {
                if (row == N - 1) {
                    ++count;
                } else {
                    columns[i] = true;
                    antidiagnoal[row + i] = true;
                    diagnoal[d] = true;
                    search(row + 1);
                    columns[i] = false;
                    antidiagnoal[row + i] = false;
                    diagnoal[d] = false;
                }
            }
        }
    }
};

int backtracking(int N) {
    BackTracking bt(N);
    bt.search(0);
    return bt.count;
}

int main(int argc, char* argv[]) {
    int nqueens = argc > 1 ? std::stoi(argv[1]) : 8;
    double start = now();
    int solutions = backtracking(nqueens);
    double end = now();
    std::cout << solutions << " solutions of " << nqueens
              << " queens puzzle.\n";
    std::cout << end - start << " seconds.\n";

    /*
    double start = now();
    int s1 = bruteforce(nqueens);
    double middle = now();
    int s2 = backtracking(nqueens);
    double end = now();
    std::cout << "brute force " << s1 << ", backtracking " << s2 << "\n";
    std::cout << "brute force " << middle - start << "\nbacktracking " << end - middle << "\n";
    */
}