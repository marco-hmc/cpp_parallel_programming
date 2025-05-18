#include <omp.h>

#include <iostream>
#include <random>

void seed(double min, double max) {
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_real_distribution<> dis(min, max);
}

double drandom() {
    static thread_local std::mt19937 generator(std::random_device{}());
    static std::uniform_real_distribution<double> distribution(-1.0, 1.0);
    return distribution(generator);
}

int main() {
    long num_trials = 1000000;  // Number of trials
    long Ncirc = 0;
    double pi, x, y, test;
    double r = 1.0;  // radius of circle. Side of square is 2*r

    seed(-r, r);  // The circle and square are centered at the origin

#pragma omp parallel for private(x, y, test) reduction(+ : Ncirc)
    for (long i = 0; i < num_trials; i++) {
        x = drandom();
        y = drandom();

        test = x * x + y * y;

        if (test <= r * r) Ncirc++;
    }

    pi = 4.0 * static_cast<double>(Ncirc) / static_cast<double>(num_trials);

    std::cout << "\n " << num_trials << " trials, pi is " << pi << " \n";
    return 0;
}