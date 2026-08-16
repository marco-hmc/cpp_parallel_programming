#include <errno.h>
#include <sched.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <fcntl.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <vector>

/*
====================================================================================================
                              进程创建和程序调用教学文档
====================================================================================================

1. 进程创建的方式：
   - fork()：创建完全独立的子进程，内存空间分离
   - vfork()：创建共享内存的子进程，性能优化版本
   - clone()：Linux特有，可精确控制资源共享
   - exec族函数：替换当前进程的程序映像

2. 进程间的关系：
   - 父子进程：通过fork创建的进程关系
   - 进程组：一组相关进程的集合
   - 会话：进程组的集合

3. 进程状态管理：
   - wait()/waitpid()：等待子进程结束
   - 僵尸进程：子进程结束但父进程未回收
   - 孤儿进程：父进程先于子进程结束

4. 性能考虑：
   - 写时复制(COW)：fork的内存优化机制
   - vfork vs fork：不同场景的选择
   - 进程创建的开销分析

====================================================================================================
*/

namespace Fork {

    // 基础fork示例
    void basicExample() {
        std::cout << "\n=== 基础fork示例 ===" << std::endl;

        std::cout << "父进程PID: " << getpid() << std::endl;

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            return;
        }

        if (pid == 0) {
            // === 子进程 ===
            std::cout << "子进程PID: " << getpid()
                      << ", 父进程PID: " << getppid() << std::endl;
            std::cout << "子进程执行一些工作..." << std::endl;
            sleep(2);
            std::cout << "子进程完成工作" << std::endl;
        } else {
            // === 父进程 ===
            std::cout << "父进程创建了子进程，PID: " << pid << std::endl;

            int status;
            pid_t waited_pid = waitpid(pid, &status, 0);

            if (waited_pid > 0) {
                if (WIFEXITED(status)) {
                    std::cout
                        << "子进程正常退出，退出码: " << WEXITSTATUS(status)
                        << std::endl;
                } else if (WIFSIGNALED(status)) {
                    std::cout << "子进程被信号终止，信号: " << WTERMSIG(status)
                              << std::endl;
                }
            }
        }
    }

    // 演示内存独立性
    void memoryIndependenceExample() {
        std::cout << "\n=== 内存独立性演示 ===" << std::endl;

        int shared_var = 100;
        std::cout << "fork前变量值: " << shared_var << std::endl;

        pid_t pid = fork();

        if (pid == 0) {
            // === 子进程 ===
            std::cout << "子进程: 初始变量值 = " << shared_var << std::endl;
            shared_var = 200;
            std::cout << "子进程: 修改后变量值 = " << shared_var << std::endl;

            // 验证内存地址
            std::cout << "子进程: 变量地址 = " << &shared_var << std::endl;
            exit(0);
        } else {
            // === 父进程 ===
            sleep(1);  // 等待子进程修改

            std::cout << "父进程: 变量值 = " << shared_var << std::endl;
            std::cout << "父进程: 变量地址 = " << &shared_var << std::endl;

            wait(nullptr);
        }

        std::cout << "内存独立性验证：父子进程的变量值不同" << std::endl;
    }

    // 多个子进程示例
    void multipleChildrenExample() {
        std::cout << "\n=== 多个子进程示例 ===" << std::endl;

        const int num_children = 3;
        std::vector<pid_t> children;

        for (int i = 0; i < num_children; i++) {
            pid_t pid = fork();

            if (pid == 0) {
                // === 子进程 ===
                std::cout << "子进程-" << i << " 启动，PID: " << getpid()
                          << std::endl;

                // 每个子进程执行不同的工作时间
                sleep(i + 1);

                std::cout << "子进程-" << i << " 完成工作" << std::endl;
                exit(i);  // 返回不同的退出码

            } else if (pid > 0) {
                // === 父进程 ===
                children.push_back(pid);
                std::cout << "父进程创建了子进程-" << i << "，PID: " << pid
                          << std::endl;
            } else {
                perror("fork failed");
                break;
            }
        }

        // 父进程等待所有子进程结束
        for (pid_t child_pid : children) {
            int status;
            pid_t waited_pid = waitpid(child_pid, &status, 0);

            if (WIFEXITED(status)) {
                std::cout << "子进程 " << waited_pid
                          << " 退出，退出码: " << WEXITSTATUS(status)
                          << std::endl;
            }
        }

        std::cout << "所有子进程已完成" << std::endl;
    }

    // 错误处理示例
    void errorHandlingExample() {
        std::cout << "\n=== 错误处理示例 ===" << std::endl;

        // 检查系统限制
        long max_processes = sysconf(_SC_CHILD_MAX);
        std::cout << "系统最大进程数: " << max_processes << std::endl;

        pid_t pid = fork();

        if (pid < 0) {
            // 错误处理
            switch (errno) {
                case EAGAIN:
                    std::cerr << "错误: 进程数量达到限制或内存不足"
                              << std::endl;
                    break;
                case ENOMEM:
                    std::cerr << "错误: 内存不足" << std::endl;
                    break;
                default:
                    perror("fork failed");
                    break;
            }
            return;
        }

        if (pid == 0) {
            std::cout << "子进程成功创建" << std::endl;
            exit(0);
        } else {
            wait(nullptr);
            std::cout << "fork成功，子进程已回收" << std::endl;
        }
    }

    void task() {
        std::cout << "=== Fork 系统调用教学示例 ===" << std::endl;

        basicExample();               // 基础示例
        memoryIndependenceExample();  // 内存独立性
        multipleChildrenExample();    // 多个子进程
        errorHandlingExample();       // 错误处理
    }

}  // namespace Fork

namespace Vfork {

    void safeExample() {
        std::cout << "\n=== vfork安全使用示例 ===" << std::endl;

        pid_t pid = vfork();

        if (pid < 0) {
            perror("vfork failed");
            return;
        }

        if (pid == 0) {
            // === 子进程 ===
            std::cout << "子进程(vfork): PID = " << getpid() << std::endl;

            // vfork的正确用法：立即exec
            char* args[] = {(char*)"/bin/echo",
                            (char*)"Hello from vfork child!", nullptr};

            if (execvp(args[0], args) == -1) {
                perror("execvp failed");
                _exit(1);  // 使用_exit而不是exit
            }
        } else {
            // === 父进程 ===
            std::cout << "父进程: 子进程PID = " << pid << std::endl;

            int status;
            waitpid(pid, &status, 0);

            if (WIFEXITED(status)) {
                std::cout << "子进程退出码: " << WEXITSTATUS(status)
                          << std::endl;
            }
        }
    }

    void performanceComparison() {
        std::cout << "\n=== fork vs vfork 性能比较 ===" << std::endl;

        const int iterations = 100;

        // 测试fork性能
        auto start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; i++) {
            pid_t pid = fork();
            if (pid == 0) {
                _exit(0);  // 子进程立即退出
            } else if (pid > 0) {
                wait(nullptr);
            }
        }

        auto fork_duration = std::chrono::high_resolution_clock::now() - start;

        // 测试vfork性能
        start = std::chrono::high_resolution_clock::now();

        for (int i = 0; i < iterations; i++) {
            pid_t pid = vfork();
            if (pid == 0) {
                _exit(0);  // 子进程立即退出
            } else if (pid > 0) {
                wait(nullptr);
            }
        }

        auto vfork_duration = std::chrono::high_resolution_clock::now() - start;

        // 输出结果
        auto fork_ms =
            std::chrono::duration_cast<std::chrono::milliseconds>(fork_duration)
                .count();
        auto vfork_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
                            vfork_duration)
                            .count();

        std::cout << "性能比较 (" << iterations << " 次创建):" << std::endl;
        std::cout << "fork时间: " << fork_ms << " ms" << std::endl;
        std::cout << "vfork时间: " << vfork_ms << " ms" << std::endl;

        if (vfork_ms < fork_ms) {
            std::cout << "vfork比fork快 " << (fork_ms - vfork_ms) << " ms"
                      << std::endl;
        }
    }

    void demonstrateDanger() {
        std::cout << "\n=== vfork危险用法演示（仅供教学） ===" << std::endl;

        int shared_var = 100;
        std::cout << "vfork前: shared_var = " << shared_var << std::endl;

        pid_t pid = vfork();

        if (pid < 0) {
            perror("vfork failed");
            return;
        }

        if (pid == 0) {
            // 危险操作：修改父进程的变量
            shared_var = 200;
            std::cout << "子进程修改shared_var为: " << shared_var << std::endl;

            // 必须使用_exit()
            _exit(0);
        } else {
            std::cout << "父进程看到shared_var = " << shared_var << std::endl;
            std::cout << "注意：这展示了vfork的危险性！" << std::endl;
        }
    }

    void task() {
        std::cout << "=== Vfork 系统调用教学示例 ===" << std::endl;

        safeExample();            // 安全使用方法
        performanceComparison();  // 性能比较
        demonstrateDanger();      // 危险用法警告
    }

}  // namespace Vfork

namespace Clone_usage {

    // 子进程/线程的执行函数
    int child_function(void* arg) {
        char* message = static_cast<char*>(arg);
        std::cout << "Child process/thread: " << message << std::endl;
        std::cout << "Child PID: " << getpid() << ", TID: " << gettid()
                  << std::endl;

        // 模拟一些工作
        sleep(2);

        std::cout << "Child finishing..." << std::endl;
        return 42;  // 返回值
    }

    // 基本clone示例 - 创建进程
    void basicCloneProcess() {
        std::cout << "=== clone() 基本进程创建示例 ===" << std::endl;

        // 为子进程分配栈空间
        const size_t stack_size = 1024 * 1024;  // 1MB
        void* stack = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (stack == MAP_FAILED) {
            perror("mmap failed");
            return;
        }

        // clone创建子进程，不共享任何资源（类似fork）
        char message[] = "Hello from clone child process!";
        void* stack_top = static_cast<char*>(stack) + stack_size;

        pid_t pid = clone(child_function, stack_top, SIGCHLD, message);

        if (pid == -1) {
            perror("clone failed");
            munmap(stack, stack_size);
            return;
        }

        std::cout << "Parent created child with PID: " << pid << std::endl;

        // 等待子进程结束
        int status;
        waitpid(pid, &status, 0);

        if (WIFEXITED(status)) {
            std::cout << "Child exited with status: " << WEXITSTATUS(status)
                      << std::endl;
        }

        // 清理栈空间
        munmap(stack, stack_size);
    }

    // clone创建线程示例
    void cloneThread() {
        std::cout << "\n=== clone() 创建线程示例 ===" << std::endl;

        const size_t stack_size = 1024 * 1024;
        void* stack = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (stack == MAP_FAILED) {
            perror("mmap failed");
            return;
        }

        char message[] = "Hello from clone thread!";
        void* stack_top = static_cast<char*>(stack) + stack_size;

        // 使用CLONE_VM | CLONE_FILES | CLONE_THREAD等创建线程
        pid_t tid = clone(child_function, stack_top,
                          CLONE_VM | CLONE_FILES | CLONE_THREAD | CLONE_SIGHAND,
                          message);

        if (tid == -1) {
            perror("clone thread failed");
            munmap(stack, stack_size);
            return;
        }

        std::cout << "Parent created thread with TID: " << tid << std::endl;
        std::cout << "Parent PID: " << getpid() << ", TID: " << gettid()
                  << std::endl;

        // 等待线程结束（注意：线程结束时不会向父进程发送SIGCHLD）
        sleep(3);

        munmap(stack, stack_size);
    }

    // 共享内存的clone示例
    volatile int shared_counter = 0;

    int shared_memory_child(void* arg) {
        for (int i = 0; i < 5; i++) {
            shared_counter++;
            std::cout << "Child incremented counter to: " << shared_counter
                      << std::endl;
            usleep(100000);  // 100ms
        }
        return 0;
    }

    void cloneSharedMemory() {
        std::cout << "\n=== clone() 共享内存示例 ===" << std::endl;

        const size_t stack_size = 1024 * 1024;
        void* stack = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (stack == MAP_FAILED) {
            perror("mmap failed");
            return;
        }

        void* stack_top = static_cast<char*>(stack) + stack_size;

        std::cout << "Initial counter value: " << shared_counter << std::endl;

        // 创建共享内存的子进程
        pid_t pid =
            clone(shared_memory_child, stack_top, CLONE_VM | SIGCHLD, nullptr);

        if (pid == -1) {
            perror("clone failed");
            munmap(stack, stack_size);
            return;
        }

        // 父进程也修改共享变量
        for (int i = 0; i < 3; i++) {
            shared_counter += 10;
            std::cout << "Parent incremented counter to: " << shared_counter
                      << std::endl;
            usleep(150000);  // 150ms
        }

        // 等待子进程结束
        int status;
        waitpid(pid, &status, 0);

        std::cout << "Final counter value: " << shared_counter << std::endl;

        munmap(stack, stack_size);
    }

    // 容器示例（需要root权限）
    void cloneNamespace() {
        std::cout << "\n=== clone() 命名空间示例（需要root权限） ==="
                  << std::endl;

        const size_t stack_size = 1024 * 1024;
        void* stack = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (stack == MAP_FAILED) {
            perror("mmap failed");
            return;
        }

        char message[] = "Hello from isolated namespace!";
        void* stack_top = static_cast<char*>(stack) + stack_size;

        // 创建新的PID命名空间
        pid_t pid =
            clone(child_function, stack_top, CLONE_NEWPID | SIGCHLD, message);

        if (pid == -1) {
            perror("clone with namespace failed");
            std::cout << "Note: This feature requires root privileges"
                      << std::endl;
            munmap(stack, stack_size);
            return;
        }

        std::cout << "Parent created child in new PID namespace" << std::endl;

        int status;
        waitpid(pid, &status, 0);
        munmap(stack, stack_size);
    }

    // 增强的clone示例
    void advancedCloneExample() {
        std::cout << "\n=== 高级clone示例：自定义资源共享 ===" << std::endl;

        const size_t stack_size = 1024 * 1024;
        void* stack = mmap(nullptr, stack_size, PROT_READ | PROT_WRITE,
                           MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (stack == MAP_FAILED) {
            perror("mmap failed");
            return;
        }

        // 创建一个文件描述符来测试CLONE_FILES
        int fd =
            open("/tmp/clone_test.txt", O_CREAT | O_WRONLY | O_TRUNC, 0644);
        if (fd == -1) {
            perror("open failed");
            munmap(stack, stack_size);
            return;
        }

        auto child_func = [](void* arg) -> int {
            int* parent_fd = (int*)arg;

            // 子进程写入同一个文件描述符
            const char* msg = "Message from child\n";
            write(*parent_fd, msg, strlen(msg));

            std::cout << "子进程写入文件，fd = " << *parent_fd << std::endl;
            return 0;
        };

        void* stack_top = static_cast<char*>(stack) + stack_size;

        // 使用CLONE_FILES共享文件描述符
        pid_t pid = clone((int (*)(void*))child_func, stack_top,
                          CLONE_FILES | SIGCHLD, &fd);

        if (pid == -1) {
            perror("clone failed");
            close(fd);
            munmap(stack, stack_size);
            return;
        }

        // 父进程也写入同一个文件
        const char* msg = "Message from parent\n";
        write(fd, msg, strlen(msg));

        wait(nullptr);
        close(fd);
        munmap(stack, stack_size);

        std::cout << "检查 /tmp/clone_test.txt 查看共享文件描述符的效果"
                  << std::endl;
    }

    void task() {
        std::cout << "=== Clone 系统调用教学示例 ===" << std::endl;

        basicCloneProcess();     // 基本进程创建
        cloneThread();           // 创建线程
        cloneSharedMemory();     // 共享内存
        advancedCloneExample();  // 高级示例
        cloneNamespace();        // 命名空间
    }

}  // namespace Clone_usage

namespace Exec {

    // 基础exec示例
    void basicExecExample() {
        std::cout << "\n=== 基础exec示例 ===" << std::endl;

        pid_t pid = fork();
        if (pid < 0) {
            perror("fork failed");
            return;
        }

        if (pid == 0) {
            // === 子进程 ===
            std::cout << "子进程准备执行ls命令" << std::endl;

            char* args[] = {(char*)"/bin/ls", (char*)"-l", (char*)"/tmp",
                            nullptr};

            if (execvp(args[0], args) == -1) {
                perror("execvp failed");
                exit(1);
            }

            // 注意：exec成功后，这行代码不会执行
            std::cout << "这行不会被执行" << std::endl;

        } else {
            // === 父进程 ===
            int status;
            waitpid(pid, &status, 0);

            std::cout << "ls命令执行完成，状态: " << status << std::endl;
        }
    }

    // exec族函数比较
    void execFamilyComparison() {
        std::cout << "\n=== exec族函数比较 ===" << std::endl;

        // execl示例
        pid_t pid1 = fork();
        if (pid1 == 0) {
            std::cout << "使用execl执行echo命令" << std::endl;
            execl("/bin/echo", "echo", "Hello from execl", (char*)nullptr);
            perror("execl failed");
            exit(1);
        } else if (pid1 > 0) {
            wait(nullptr);
        }

        // execv示例
        pid_t pid2 = fork();
        if (pid2 == 0) {
            std::cout << "使用execv执行echo命令" << std::endl;
            char* args[] = {(char*)"echo", (char*)"Hello from execv", nullptr};
            execv("/bin/echo", args);
            perror("execv failed");
            exit(1);
        } else if (pid2 > 0) {
            wait(nullptr);
        }

        // execvp示例（自动查找PATH）
        pid_t pid3 = fork();
        if (pid3 == 0) {
            std::cout << "使用execvp执行echo命令（自动查找PATH）" << std::endl;
            char* args[] = {(char*)"echo", (char*)"Hello from execvp", nullptr};
            execvp("echo", args);
            perror("execvp failed");
            exit(1);
        } else if (pid3 > 0) {
            wait(nullptr);
        }
    }

    // 环境变量传递示例
    void environmentExample() {
        std::cout << "\n=== 环境变量传递示例 ===" << std::endl;

        pid_t pid = fork();
        if (pid == 0) {
            // 设置环境变量
            char* env[] = {(char*)"MY_VAR=Hello World",
                           (char*)"PATH=/bin:/usr/bin", nullptr};

            char* args[] = {(char*)"/bin/sh", (char*)"-c",
                            (char*)"echo $MY_VAR", nullptr};

            std::cout << "子进程执行shell命令显示环境变量" << std::endl;
            execve("/bin/sh", args, env);
            perror("execve failed");
            exit(1);
        } else {
            wait(nullptr);
        }
    }

    // 错误处理示例
    void errorHandlingExample() {
        std::cout << "\n=== exec错误处理示例 ===" << std::endl;

        pid_t pid = fork();
        if (pid == 0) {
            // 尝试执行不存在的程序
            char* args[] = {(char*)"nonexistent_program", nullptr};

            if (execvp(args[0], args) == -1) {
                switch (errno) {
                    case ENOENT:
                        std::cerr << "错误: 找不到可执行文件" << std::endl;
                        break;
                    case EACCES:
                        std::cerr << "错误: 没有执行权限" << std::endl;
                        break;
                    case ENOMEM:
                        std::cerr << "错误: 内存不足" << std::endl;
                        break;
                    default:
                        perror("execvp failed");
                        break;
                }
                exit(1);
            }
        } else {
            int status;
            waitpid(pid, &status, 0);

            if (WIFEXITED(status) && WEXITSTATUS(status) == 1) {
                std::cout << "exec失败被正确处理" << std::endl;
            }
        }
    }

    void task() {
        std::cout << "=== Exec 系统调用教学示例 ===" << std::endl;

        basicExecExample();      // 基础示例
        execFamilyComparison();  // exec族函数比较
        environmentExample();    // 环境变量
        errorHandlingExample();  // 错误处理
    }

}  // namespace Exec

// 综合示例：简单的shell实现
namespace SimpleShell {

    void demonstrateShell() {
        std::cout << "\n=== 简单Shell实现演示 ===" << std::endl;

        const char* commands[] = {"ls -l", "echo 'Hello World'", "date",
                                  nullptr};

        for (int i = 0; commands[i] != nullptr; i++) {
            std::cout << "\n执行命令: " << commands[i] << std::endl;

            pid_t pid = fork();
            if (pid == 0) {
                // 子进程执行命令
                char* args[] = {(char*)"/bin/sh", (char*)"-c",
                                (char*)commands[i], nullptr};

                execvp(args[0], args);
                perror("exec failed");
                exit(1);

            } else if (pid > 0) {
                // 父进程等待
                int status;
                waitpid(pid, &status, 0);

                if (WIFEXITED(status)) {
                    std::cout << "命令执行完成，退出码: " << WEXITSTATUS(status)
                              << std::endl;
                }
            } else {
                perror("fork failed");
            }
        }
    }

    void task() {
        std::cout << "=== 简单Shell演示 ===" << std::endl;
        demonstrateShell();
    }

}  // namespace SimpleShell

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "     进程创建和程序调用教学示例" << std::endl;
    std::cout << "========================================" << std::endl;

    Fork::task();         // fork示例
    Vfork::task();        // vfork示例
    Clone_usage::task();  // clone示例
    Exec::task();         // exec示例
    SimpleShell::task();  // 综合示例

    std::cout << "\n重要提示：" << std::endl;
    std::cout << "1. fork创建独立进程，vfork优化性能但有限制" << std::endl;
    std::cout << "2. clone提供最大灵活性，是线程和容器的基础" << std::endl;
    std::cout << "3. exec族函数替换进程映像，不创建新进程" << std::endl;
    std::cout << "4. 总是检查返回值并正确处理错误" << std::endl;
    std::cout << "5. 使用wait/waitpid回收子进程，避免僵尸进程" << std::endl;
    std::cout << "6. 在vfork中使用_exit而不是exit" << std::endl;

    return 0;
}
