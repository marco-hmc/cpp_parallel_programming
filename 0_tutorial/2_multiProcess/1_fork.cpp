#include <unistd.h>

#include <iostream>

/*
  1. fork()的返回值是什么
    * 负值：表示创建进程失败。
    * 零：表示当前代码在子进程中执行。子进程中fork()的返回值为0。
    * 正值：表示当前代码在父进程中执行。返回值是新创建的子进程的PID。

  2. 怎么理解fork()做了什么？上下文是怎么保存下来的？
    * fork()会复制当前进程的上下文，包括代码段、数据段、堆栈等。这样子进程就可以继续执行父进程的代码。
    * 子进程会复制父进程的内存映像，但是子进程会有自己的内存空间，父子进程的内存空间是独立的。

  3. 为什么fork()返回两次？
    * fork()返回两次，是因为子进程和父进程都会执行fork()后面的代码。子进程返回0，父进程返回子进程的PID。

  4. 什么时候fork()会失败？
    * fork()失败的原因有很多，比如进程数达到上限、内存不足等。fork()失败时，返回值是-1。
*/

int main() {
    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "Fork failed" << '\n';
        return 1;
    }

    if (pid == 0) {
        std::cout << "This is the child process. PID: " << getpid() << '\n';
    } else {
        std::cout << "This is the parent process. Child PID: " << pid << '\n';
    }

    return 0;
}
