#include <errno.h>
#include <sys/wait.h>
#include <unistd.h>

#include <csignal>
#include <cstring>
#include <iostream>

/*
====================================================================================================
                                    信号 (Signal) 教学文档
====================================================================================================

1. 什么是信号？
   - 信号是一种软件中断，用于进程间通信和异步事件通知
   - 信号是异步的，可以在任何时候被发送和接收
   - 每个信号都有一个唯一的编号和名称
   - 信号是UNIX/Linux系统中最古老的IPC机制之一

2. 信号的特点：
   - 异步性：信号可以在任何时候到达
   - 简洁性：信号只能传递信号类型，不能传递复杂数据
   - 可靠性：现代系统支持可靠信号（不会丢失）
   - 优先级：某些信号无法被忽略或阻塞（如SIGKILL、SIGSTOP）

3. 常见信号类型：
   - SIGINT (2)：中断信号，通常由Ctrl+C产生
   - SIGTERM (15)：终止信号，请求进程正常终止
   - SIGKILL (9)：强制终止信号，无法被捕获或忽略
   - SIGCHLD (17)：子进程状态改变信号
   - SIGALRM (14)：定时器信号
   - SIGUSR1/SIGUSR2：用户自定义信号

4. 信号处理方式：
   - 默认处理：执行系统默认动作（终止、忽略、停止等）
   - 忽略信号：使用SIG_IGN
   - 捕获信号：注册自定义处理函数
   - 阻塞信号：暂时屏蔽某些信号

5. 核心系统调用：
   - signal()：注册信号处理函数（简单接口）
   - sigaction()：注册信号处理函数（高级接口）
   - kill()：向进程发送信号
   - raise()：向当前进程发送信号
   - alarm()：设置定时器信号
   - pause()：等待信号

====================================================================================================
*/

namespace Signal {

    // 全局变量用于演示
    volatile sig_atomic_t signal_received = 0;
    volatile sig_atomic_t alarm_count = 0;
    volatile sig_atomic_t safe_flag = 0;  // 添加全局标志

    // 示例1：基础信号处理
    void basicSignalHandler(int signo) {
        const char *signal_name;
        switch (signo) {
            case SIGINT:
                signal_name = "SIGINT";
                break;
            case SIGTERM:
                signal_name = "SIGTERM";
                break;
            case SIGUSR1:
                signal_name = "SIGUSR1";
                break;
            case SIGUSR2:
                signal_name = "SIGUSR2";
                break;
            default:
                signal_name = "UNKNOWN";
                break;
        }

        // 注意：在信号处理函数中只应使用异步信号安全的函数
        write(STDOUT_FILENO, "接收到信号: ", 12);
        write(STDOUT_FILENO, signal_name, strlen(signal_name));
        write(STDOUT_FILENO, "\n", 1);

        signal_received = signo;
    }

    void basicExample() {
        std::cout << "\n=== 示例1：基础信号处理 ===" << std::endl;
        std::cout << "进程PID: " << getpid() << std::endl;
        std::cout << "注册信号处理函数..." << std::endl;

        // 注册信号处理函数
        signal(SIGINT, basicSignalHandler);
        signal(SIGTERM, basicSignalHandler);
        signal(SIGUSR1, basicSignalHandler);
        signal(SIGUSR2, basicSignalHandler);

        std::cout
            << "等待信号（按Ctrl+C发送SIGINT，或从另一个终端使用kill命令）"
            << std::endl;
        std::cout << "命令示例: kill -USR1 " << getpid() << std::endl;

        // 等待信号
        for (int i = 0; i < 10 && signal_received == 0; i++) {
            std::cout << "等待中... (" << (i + 1) << "/10)" << std::endl;
            sleep(1);
        }

        if (signal_received) {
            std::cout << "程序因接收到信号 " << signal_received << " 而结束"
                      << std::endl;
        } else {
            std::cout << "等待超时，程序结束" << std::endl;
        }
    }

    // 示例2：父子进程间信号通信
    void parentChildExample() {
        std::cout << "\n=== 示例2：父子进程间信号通信 ===" << std::endl;

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            return;
        }

        if (pid > 0) {
            // === 父进程 ===
            std::cout << "父进程PID: " << getpid() << ", 子进程PID: " << pid
                      << std::endl;

            // 等待一下让子进程准备好
            sleep(1);

            // 向子进程发送信号
            std::cout << "父进程向子进程发送SIGUSR1信号" << std::endl;
            if (kill(pid, SIGUSR1) == -1) {
                perror("kill failed");
            }

            sleep(1);

            std::cout << "父进程向子进程发送SIGUSR2信号" << std::endl;
            if (kill(pid, SIGUSR2) == -1) {
                perror("kill failed");
            }

            sleep(1);

            // 发送终止信号
            std::cout << "父进程向子进程发送SIGTERM信号" << std::endl;
            if (kill(pid, SIGTERM) == -1) {
                perror("kill failed");
            }

            // 等待子进程结束
            int status;
            wait(&status);
            std::cout << "子进程结束，状态: " << status << std::endl;

        } else {
            // === 子进程 ===
            std::cout << "子进程启动，PID: " << getpid() << std::endl;

            // 子进程的信号处理函数
            auto childHandler = [](int signo) {
                switch (signo) {
                    case SIGUSR1:
                        write(STDOUT_FILENO, "子进程: 收到SIGUSR1，执行任务1\n",
                              29);
                        break;
                    case SIGUSR2:
                        write(STDOUT_FILENO, "子进程: 收到SIGUSR2，执行任务2\n",
                              29);
                        break;
                    case SIGTERM:
                        write(STDOUT_FILENO, "子进程: 收到SIGTERM，准备退出\n",
                              29);
                        signal_received = SIGTERM;
                        break;
                }
            };

            signal(SIGUSR1, childHandler);
            signal(SIGUSR2, childHandler);
            signal(SIGTERM, childHandler);

            // 子进程循环等待信号
            while (signal_received != SIGTERM) {
                pause();  // 暂停等待信号
            }

            std::cout << "子进程正常退出" << std::endl;
        }
    }

    // 示例3：定时器信号（SIGALRM）
    void alarmHandler(int signo) {
        alarm_count++;
        write(STDOUT_FILENO, "定时器触发!\n", 12);
    }

    void alarmExample() {
        std::cout << "\n=== 示例3：定时器信号（SIGALRM） ===" << std::endl;

        signal(SIGALRM, alarmHandler);

        std::cout << "设置定时器，每2秒触发一次" << std::endl;

        // 设置重复定时器
        for (int i = 0; i < 3; i++) {
            alarm(2);  // 2秒后触发SIGALRM

            std::cout << "等待定时器信号... (第" << (i + 1) << "次)"
                      << std::endl;
            pause();  // 等待信号

            std::cout << "定时器触发次数: " << alarm_count << std::endl;
        }

        std::cout << "定时器示例完成" << std::endl;
    }

    // 示例4：信号集操作和信号屏蔽
    void signalSetExample() {
        std::cout << "\n=== 示例4：信号集操作和信号屏蔽 ===" << std::endl;

        sigset_t new_mask, old_mask, pending_mask;

        // 初始化信号集
        sigemptyset(&new_mask);
        sigaddset(&new_mask, SIGUSR1);
        sigaddset(&new_mask, SIGUSR2);

        std::cout << "当前进程PID: " << getpid() << std::endl;

        // 阻塞SIGUSR1和SIGUSR2信号
        if (sigprocmask(SIG_BLOCK, &new_mask, &old_mask) == -1) {
            perror("sigprocmask failed");
            return;
        }

        std::cout << "已阻塞SIGUSR1和SIGUSR2信号" << std::endl;
        std::cout << "现在向自己发送这些信号..." << std::endl;

        // 向自己发送被阻塞的信号
        raise(SIGUSR1);
        raise(SIGUSR2);

        std::cout << "信号已发送，但被阻塞" << std::endl;

        // 检查pending信号
        if (sigpending(&pending_mask) == 0) {
            std::cout << "检查pending信号:" << std::endl;
            if (sigismember(&pending_mask, SIGUSR1)) {
                std::cout << "  SIGUSR1 在pending集合中" << std::endl;
            }
            if (sigismember(&pending_mask, SIGUSR2)) {
                std::cout << "  SIGUSR2 在pending集合中" << std::endl;
            }
        }

        std::cout << "等待3秒..." << std::endl;
        sleep(3);

        // 注册信号处理函数
        signal(SIGUSR1, basicSignalHandler);
        signal(SIGUSR2, basicSignalHandler);

        std::cout << "解除信号阻塞..." << std::endl;

        // 恢复原来的信号屏蔽字
        if (sigprocmask(SIG_SETMASK, &old_mask, nullptr) == -1) {
            perror("sigprocmask failed");
            return;
        }

        std::cout << "信号阻塞已解除，pending信号应该被处理" << std::endl;
        sleep(1);  // 给信号处理一些时间
    }

    // 示例5：使用sigaction的高级信号处理
    void advancedSignalHandler(int signo, siginfo_t *info, void *context) {
        std::cout << "\n=== 高级信号处理 ===" << std::endl;
        std::cout << "信号编号: " << signo << std::endl;
        std::cout << "发送者PID: " << info->si_pid << std::endl;
        std::cout << "发送者UID: " << info->si_uid << std::endl;
        std::cout << "信号来源: ";

        switch (info->si_code) {
            case SI_USER:
                std::cout << "用户进程发送" << std::endl;
                break;
            case SI_KERNEL:
                std::cout << "内核发送" << std::endl;
                break;
            default:
                std::cout << "其他来源 (" << info->si_code << ")" << std::endl;
                break;
        }
    }

    void sigactionExample() {
        std::cout << "\n=== 示例5：使用sigaction的高级信号处理 ==="
                  << std::endl;

        struct sigaction sa;
        struct sigaction old_sa;

        // 设置信号处理结构
        sa.sa_sigaction = advancedSignalHandler;
        sigemptyset(&sa.sa_mask);
        sa.sa_flags = SA_SIGINFO;  // 使用sa_sigaction而不是sa_handler

        // 注册信号处理函数
        if (sigaction(SIGUSR1, &sa, &old_sa) == -1) {
            perror("sigaction failed");
            return;
        }

        std::cout << "已注册高级信号处理函数" << std::endl;
        std::cout << "进程PID: " << getpid() << std::endl;
        std::cout << "发送SIGUSR1信号..." << std::endl;

        // 向自己发送信号
        raise(SIGUSR1);

        sleep(1);

        // 恢复原来的信号处理
        if (sigaction(SIGUSR1, &old_sa, nullptr) == -1) {
            perror("sigaction restore failed");
        }

        std::cout << "高级信号处理示例完成" << std::endl;
    }

    // 信号安全的处理函数
    void safeSignalHandler(int signo) {
        // 只使用异步信号安全的操作
        safe_flag = 1;  // sig_atomic_t类型的简单赋值是安全的

        // 使用write而不是cout（write是异步信号安全的）
        const char msg[] = "安全的信号处理\n";
        write(STDOUT_FILENO, msg, sizeof(msg) - 1);
    }

    // 示例6：信号安全编程
    void safeProgrammingExample() {
        std::cout << "\n=== 示例6：信号安全编程 ===" << std::endl;

        safe_flag = 0;  // 重置标志

        signal(SIGINT, safeSignalHandler);

        std::cout << "演示信号安全编程" << std::endl;
        std::cout << "按Ctrl+C测试信号处理（或等待5秒自动结束）" << std::endl;

        // 主循环
        for (int i = 0; i < 5 && safe_flag == 0; i++) {
            std::cout << "工作中... " << (i + 1) << "/5" << std::endl;
            sleep(1);
        }

        if (safe_flag) {
            std::cout << "检测到信号，程序安全退出" << std::endl;
        } else {
            std::cout << "正常完成工作" << std::endl;
        }
    }

    void task() {
        std::cout << "=== 信号 (Signal) 教学示例 ===" << std::endl;

        basicExample();            // 基础信号处理
        parentChildExample();      // 父子进程通信
        alarmExample();            // 定时器信号
        signalSetExample();        // 信号集操作
        sigactionExample();        // 高级信号处理
        safeProgrammingExample();  // 信号安全编程
    }

}  // namespace Signal

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "      进程间通信 - 信号教学示例" << std::endl;
    std::cout << "========================================" << std::endl;

    Signal::task();

    std::cout << "\n重要提示：" << std::endl;
    std::cout << "1. 信号处理函数应该尽可能简单" << std::endl;
    std::cout << "2. 在信号处理函数中只使用异步信号安全的函数" << std::endl;
    std::cout << "3. 使用volatile sig_atomic_t类型处理全局标志" << std::endl;
    std::cout << "4. SIGKILL和SIGSTOP无法被捕获或忽略" << std::endl;
    std::cout << "5. 可以使用 'man 7 signal' 查看详细的信号列表" << std::endl;

    return 0;
}