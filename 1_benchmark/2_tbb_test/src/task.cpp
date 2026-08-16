#include "task.h"

#include <spdlog/sinks/basic_file_sink.h>
#include <spdlog/spdlog.h>
#include <tbb/task_group.h>

#include <cassert>
#include <chrono>

void busyWait1s() {
    auto start = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::high_resolution_clock::now() - start)
               .count() < 1) {
    }
}

int busyWait200ms() {
    auto start = std::chrono::high_resolution_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::high_resolution_clock::now() - start)
               .count() < 200) {
    }
    return 1;
}

int fib(int value) {
    if (value == 0) {
        return 1;
    }
    if (value == 1) {
        return 1;
    }
    return fib(value - 1) + fib(value - 2);
}

void countNumberWithLock(int counter) {
    int value = 0;
    std::mutex mtx;
    for (int i = 0; i < counter; ++i) {
        std::lock_guard<std::mutex> lock(mtx);
        ++value;
    }
    assert(value == counter);
}

void countNumberWithoutLock(int counter) {
    int value = 0;
    for (int i = 0; i < counter; ++i) {
        ++value;
    }
    assert(value == counter);
}

void countNumber(int counter) {
    int value = 0;
    for (int i = 0; i < counter; ++i) {
        ++value;
    }
    assert(value == counter);
}

void task50ms() { countNumber(120'000'000); }

void taskNear50ms() { countNumber(240'000'000); }

void task200ms() { countNumber(480'000'000); }

bool isPrime(int n) {
    if (n <= 1) return false;
    for (int i = 2; i <= std::sqrt(n); ++i) {
        if (n % i == 0) return false;
    }
    return true;
}

void findPrimesInRange(int start, int end, std::vector<int>& primes,
                       std::mutex& primes_mutex) {
    std::vector<int> local_primes;
    for (int i = start; i <= end; ++i) {
        if (isPrime(i)) {
            local_primes.push_back(i);
        }
    }
    std::lock_guard<std::mutex> lock(primes_mutex);
    primes.insert(primes.end(), local_primes.begin(), local_primes.end());
}