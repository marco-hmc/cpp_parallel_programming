#include <csignal>
#include <iostream>

void signalHandler(int signal) {
    std::cout << "Received signal: " << signal << '\n';
}

int main() {
    signal(SIGINT, signalHandler);

    std::cout << "Running..." << '\n';

    while (true) {
    }

    return 0;
}