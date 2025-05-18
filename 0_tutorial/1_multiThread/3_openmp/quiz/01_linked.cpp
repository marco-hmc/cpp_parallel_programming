#include <omp.h>

#include <iostream>
#include <memory>
#include <vector>

#define N 5
#define FS 38
#define NMAX 10

struct Node {
    int data;
    int fibData;
    std::shared_ptr<Node> next;
};

int fib(int n) {
    if (n < 2) {
        return n;
    } else {
        return fib(n - 1) + fib(n - 2);
    }
}

void processWork(std::shared_ptr<Node> p) { p->fibData = fib(p->data); }

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
    double start, end;
    std::shared_ptr<Node> head = initList();
    std::shared_ptr<Node> p = head;
    std::vector<std::shared_ptr<Node>> nodeArray(NMAX);
    int count = 0;

    std::cout << "Process linked list\n";
    std::cout << "  Each linked list node will be processed by function "
                 "'processWork()'\n";
    std::cout << "  Each ll node will compute " << N
              << " fibonacci numbers beginning with " << FS << "\n";

    start = omp_get_wtime();
    while (p != nullptr) {
        processWork(p);
        p = p->next;
    }
    end = omp_get_wtime();
    std::cout << "Serial Compute Time: " << end - start << " seconds\n";

    p = head;
    start = omp_get_wtime();
    {
        // Count number of items in the list
        while (p != nullptr) {
            p = p->next;
            count++;
        }

        // Traverse the list and collect pointers into an array
        p = head;
        for (int i = 0; i < count; ++i) {
            nodeArray[i] = p;
            p = p->next;
        }

// Do the work in parallel
#pragma omp parallel
        {
#pragma omp single
            std::cout << " " << omp_get_num_threads() << " threads\n";

#pragma omp for schedule(static, 1)
            for (int i = 0; i < count; ++i) {
                processWork(nodeArray[i]);
            }
        }
    }
    end = omp_get_wtime();

    p = head;
    while (p != nullptr) {
        std::cout << p->data << " : " << p->fibData << "\n";
        p = p->next;
    }

    std::cout << "Parallel Compute Time: " << end - start << " seconds\n";

    return 0;
}