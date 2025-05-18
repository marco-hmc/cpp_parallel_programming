#include <iostream>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

/*
  1. **为什么`args`后面要有一个`NULL`，可以是`nullptr`吗？**

      `args`数组的最后一个元素必须是`NULL`，以标识参数列表的结束。
      这是因为`execvp`函数需要知道何时停止处理`args`数组。
      使用`nullptr`代替`NULL`是可以的，也是推荐的做法。

  2. **`execvp`怎么用？各个参数的意义是什么？**

      ```cpp
      int execvp(const char *file, char *const argv[]);
      ```
      -
      `file`：要执行的文件名。
          如果`file`中不包含路径（即不含`/`），则`execvp`会在环境变量`PATH`指定的目录中查找该文件。
      -
      `argv`：一个字符串数组，其中包含要传递给`file`指定的程序的参数。
          `argv[0]`通常是程序的名称，数组的最后一个元素必须是`NULL`，以标识参数列表的结束。

  3. **为什么`execvp()`的函数接受的参数是`(args[0],
  args)`。第二个参数不是包括了第一个参数吗？**

      `execvp`函数的设计遵循UNIX传统，其中`argv[0]`通常是程序的名称，这是一种约定。
      虽然`args`数组的第一个元素（`args[0]`）确实被包含在第二个参数`args`中，但`execvp`函数需要这种格式来正确解析和执行程序。
      这样设计允许程序知道自己是如何被调用的，因为`argv[0]`可以与实际的可执行文件名不同。

  4. **`waitpid`的参数是什么意思？**
      ```cpp
      pid_t waitpid(pid_t pid, int *status, int options);
      ```

      - `pid`：指定要等待的子进程的进程ID。特殊值（如`-1`）表示等待任何子进程。

      `status`：一个指针，指向一个整数，在这里函数会存储子进程的终止状态。通过这个参数，父进程可以了解子进程的退出原因。

      `options`：用于修改`waitpid`行为的选项，例如`WNOHANG`表示非阻塞模式，即如果没有子进程退出，`waitpid`会立即返回0而不是阻塞。

      `waitpid`函数的返回值有几种情况：成功时返回子进程的PID；如果设置了`WNOHANG`且没有子进程退出，则返回0；出错时返回-1。
*/

int main() {
    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "Fork failed" << '\n';
        return 1;
    }

    if (pid == 0) {
        char *args[] = {(char *)"/bin/ls", (char *)"-l", nullptr};
        execvp(args[0], args);
        std::cerr << "Exec failed" << '\n';
        return 1;
    } else {
        int status;
        waitpid(pid, &status, 0);
        std::cout << "Child finished with status " << status << '\n';
    }

    return 0;
}
