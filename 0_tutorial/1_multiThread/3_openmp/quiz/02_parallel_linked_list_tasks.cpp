#include <math.h>
#include <omp.h>

#include <iostream>
#include <memory>

#ifndef N
#define N 5
#endif
#ifndef FS
#define FS 38
#endif

struct Node {
    int data;
    int fibData;
    std::shared_ptr<Node> next;
};

int fib(int n) {
    if (n < 2) {
        return n;
    }
    return fib(n - 1) + fib(n - 2);
}

void processWork(const std::shared_ptr<Node>& p) { p->fibData = fib(p->data); }

std::shared_ptr<Node> initList() {
    std::shared_ptr<Node> head = std::make_shared<Node>();
    std::shared_ptr<Node> p = head;
    p->data = FS;
    p->fibData = 0;

    for (int i = 0; i < N; ++i) {
        p->next = std::make_shared<Node>();
        p = p->next;
        p->data = FS + i + 1;
        p->fibData = i + 1;
    }

    p->next = nullptr;
    return head;
}

int main() {
    double start = NAN;
    double end = NAN;
    std::shared_ptr<Node> head = initList();
    std::shared_ptr<Node> p = head;

    std::cout << "Process linked list\n";
    std::cout << "  Each linked list node will be processed by function "
                 "'processWork()'\n";
    std::cout << "  Each ll node will compute " << N
              << " fibonacci numbers beginning with " << FS << "\n";

    start = omp_get_wtime();

#pragma omp parallel
    {
#pragma omp master
        std::cout << "Threads: " << omp_get_num_threads() << std::endl;
#pragma omp single
        {
            p = head;
            while (p) {
#pragma omp task firstprivate(p)  // firstprivate is required
                processWork(p);
                p = p->next;
            }
        }
    }

    end = omp_get_wtime();

    p = head;
    while (p != nullptr) {
        std::cout << p->data << " : " << p->fibData << std::endl;
        p = p->next;
    }
    std::cout << "Compute Time: " << end - start << " seconds" << std::endl;
    return 0;
}