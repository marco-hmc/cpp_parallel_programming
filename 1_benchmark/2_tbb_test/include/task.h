#include "utils.h"

NO_OPTIMIZE void busyWait1s();
NO_OPTIMIZE int busyWait200ms();
NO_OPTIMIZE int fib(int value);

NO_OPTIMIZE void countNumberWithLock(int counter);
NO_OPTIMIZE void countNumberWithoutLock(int counter);

NO_OPTIMIZE void task50ms();
NO_OPTIMIZE void taskNear50ms();
NO_OPTIMIZE void task200ms();

NO_OPTIMIZE bool isPrime(int n);

NO_OPTIMIZE void findPrimesInRange(int start, int end, std::vector<int>& primes,
                                   std::mutex& primes_mutex);