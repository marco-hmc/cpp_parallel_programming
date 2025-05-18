#include <cstring>
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

#define BUFFER_SIZE 25
#define READ_END 0
#define WRITE_END 1

/*
  1. `pipe()`函数用于创建一个管道，以实现进程间的通信。

      它接受一个参数，即一个长度为2的整型数组。
      成功时，数组的两个元素被设置为管道的两个文件描述符：
        `fd[0]`用于读，`fd[1]`用于写。
      如果成功，函数返回0；失败则返回-1。

      ```cpp
      #include <unistd.h>
      int fd[2];
      if (pipe(fd) == -1) {
          // 错误处理
      }
      ```

  2.`close()`函数用于关闭一个文件描述符，释放它所占用的资源。

      如果关闭成功，返回0；失败则返回-1。

      ```cpp
      #include <unistd.h>
      close(fd[0]); // 关闭读端
      close(fd[1]); // 关闭写端
      ```

  3.`wait()`函数用于使父进程等待子进程结束，并回收子进程所占用的资源。

      它接受一个指向`int`的指针作为参数，用来存储子进程的退出状态。如果成功，返回子进程的PID；失败则返回-1。

      ```cpp
      #include <sys/wait.h>
      int status;
      pid_t pid = wait(&status);
      if (pid == -1) {
          // 错误处理
      }
      ```

  4.`read()`函数用于从文件描述符中读取数据。

      它接受三个参数：文件描述符、指向数据缓冲区的指针和要读取的字节数。
      成功时，返回读取的字节数；如果到达文件末尾，返回0；失败则返回-1。

      ```cpp
      #include <unistd.h>
      char buffer[1024];
      ssize_t bytesRead = read(fd[0], buffer, sizeof(buffer));
      if (bytesRead == -1) {
          // 错误处理
      }
      ```
  5. 如果不close会怎么样？

      如果不`close()`管道的写端，可能会导致读端的`read()`操作无法正常检测到文件结束（EOF）。
      在管道通信中，当所有的写端被关闭后，读端在读取完所有数据之后的下一次`read()`调用将返回0，表示到达了文件末尾。
      如果写端没有被关闭，读端可能会一直阻塞在`read()`调用上，等待数据到来，即使写端已经不再写入任何数据。

  6. 如果不wait()会怎么样？

      如果不调用`wait()`，子进程将成为僵尸进程（zombie process）
      即子进程已经结束，但是其状态信息仍然保存在系统中，等待父进程通过`wait()`或`waitpid()`来回收。
      这样做的目的是保留子进程的退出状态，直到父进程准备好来查询。
      如果父进程不调用`wait()`，那么随着时间的推移，僵尸进程可能会积累，占用系统资源。
      在某些情况下，如果父进程先于子进程退出，子进程将被init进程（或其他系统进程）收养，该进程将负责调用`wait()`来清理状态信息。
*/

int main() {
    char write_msg[BUFFER_SIZE] = "Hello, child!";
    char read_msg[BUFFER_SIZE];
    int fd[2];

    if (pipe(fd) == -1) {
        std::cerr << "Pipe failed" << '\n';
        return 1;
    }

    pid_t pid = fork();

    if (pid < 0) {
        std::cerr << "Fork Failed" << '\n';
        return 1;
    }
    if (pid > 0) {
        close(fd[READ_END]);
        write(fd[WRITE_END], write_msg, strlen(write_msg) + 1);
        close(fd[WRITE_END]);
        wait(nullptr);
    } else {
        close(fd[WRITE_END]);
        read(fd[READ_END], read_msg, BUFFER_SIZE);
        std::cout << "read: " << read_msg << '\n';
        close(fd[READ_END]);
    }

    return 0;
}
