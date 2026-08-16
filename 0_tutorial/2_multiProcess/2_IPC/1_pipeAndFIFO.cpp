#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

#define BUFFER_SIZE 1024
#define READ_END 0
#define WRITE_END 1
#define FIFO_NAME "/tmp/myfifo"

/*
====================================================================================================
                                    PIPE 教学文档
====================================================================================================

1. 什么是PIPE（管道）？
   - 管道是一种进程间通信（IPC）机制
   - 允许一个进程的输出直接连接到另一个进程的输入
   - 有两种类型：匿名管道（pipe）和命名管道（FIFO/named pipe）

2. 匿名管道（Anonymous Pipe）特点：
   - 只能在有亲缘关系的进程间使用（父子进程）
   - 单向通信（半双工）
   - 数据流向：写端 → 管道缓冲区 → 读端
   - 读取是消费性的（读取后数据被移除）

3. 核心系统调用：
   - pipe()：创建管道
   - read()：从管道读取数据
   - write()：向管道写入数据
   - close()：关闭管道端点

4. 管道的工作原理：
   ```
   父进程           管道             子进程
   write() -----> [缓冲区] -----> read()
                   (FIFO)
   ```

5. 重要概念：
   - 阻塞性：如果管道为空，读操作会阻塞；如果管道满，写操作会阻塞
   - EOF检测：当所有写端关闭后，读端才能检测到EOF
   - 原子性：小于PIPE_BUF字节的写操作是原子的
====================================================================================================
*/

namespace PIPE {

    // 示例1：基础的父子进程通信
    void basicExample() {
        std::cout << "\n=== 示例1：基础管道通信 ===" << std::endl;

        int fd[2];  // fd[0]读端，fd[1]写端
        char write_msg[] = "Hello from parent!";
        char read_msg[BUFFER_SIZE];

        // 步骤1：创建管道
        if (pipe(fd) == -1) {
            perror("pipe creation failed");
            return;
        }

        std::cout << "管道创建成功：读端fd=" << fd[READ_END]
                  << "，写端fd=" << fd[WRITE_END] << std::endl;

        // 步骤2：创建子进程
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            close(fd[READ_END]);
            close(fd[WRITE_END]);
            return;
        }

        if (pid > 0) {
            // === 父进程（写入者） ===
            std::cout << "父进程 PID=" << getpid() << " 开始写入数据"
                      << std::endl;

            // 关闭不需要的读端
            close(fd[READ_END]);

            // 写入数据到管道
            ssize_t bytes_written =
                write(fd[WRITE_END], write_msg, strlen(write_msg) + 1);
            if (bytes_written == -1) {
                perror("write failed");
            } else {
                std::cout << "父进程写入了 " << bytes_written << " 字节"
                          << std::endl;
            }

            // 关闭写端（重要：让子进程能检测到EOF）
            close(fd[WRITE_END]);

            // 等待子进程结束
            int status;
            wait(&status);
            std::cout << "子进程结束，状态码：" << status << std::endl;

        } else {
            // === 子进程（读取者） ===
            std::cout << "子进程 PID=" << getpid() << " 开始读取数据"
                      << std::endl;

            // 关闭不需要的写端
            close(fd[WRITE_END]);

            // 从管道读取数据
            ssize_t bytes_read = read(fd[READ_END], read_msg, BUFFER_SIZE);
            if (bytes_read == -1) {
                perror("read failed");
            } else if (bytes_read == 0) {
                std::cout << "子进程：读到EOF，管道已关闭" << std::endl;
            } else {
                std::cout << "子进程读取了 " << bytes_read << " 字节：\""
                          << read_msg << "\"" << std::endl;
            }

            // 关闭读端
            close(fd[READ_END]);
        }
    }

    // 示例2：双向通信（使用两个管道）
    void bidirectionalExample() {
        std::cout << "\n=== 示例2：双向通信 ===" << std::endl;

        int pipe1[2], pipe2[2];  // pipe1: 父→子, pipe2: 子→父
        char parent_msg[] = "Message from parent";
        char child_msg[] = "Response from child";
        char buffer[BUFFER_SIZE];

        // 创建两个管道
        if (pipe(pipe1) == -1 || pipe(pipe2) == -1) {
            perror("pipe creation failed");
            return;
        }

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            return;
        }

        if (pid > 0) {
            // === 父进程 ===
            // 关闭不需要的端点
            close(pipe1[READ_END]);   // 父进程不从pipe1读
            close(pipe2[WRITE_END]);  // 父进程不向pipe2写

            // 发送消息给子进程
            write(pipe1[WRITE_END], parent_msg, strlen(parent_msg) + 1);
            close(pipe1[WRITE_END]);  // 发送完毕，关闭写端

            std::cout << "父进程发送：" << parent_msg << std::endl;

            // 接收子进程的回复
            ssize_t bytes_read = read(pipe2[READ_END], buffer, BUFFER_SIZE);
            if (bytes_read > 0) {
                std::cout << "父进程接收：" << buffer << std::endl;
            }

            close(pipe2[READ_END]);
            wait(nullptr);

        } else {
            // === 子进程 ===
            // 关闭不需要的端点
            close(pipe1[WRITE_END]);  // 子进程不向pipe1写
            close(pipe2[READ_END]);   // 子进程不从pipe2读

            // 接收父进程的消息
            ssize_t bytes_read = read(pipe1[READ_END], buffer, BUFFER_SIZE);
            if (bytes_read > 0) {
                std::cout << "子进程接收：" << buffer << std::endl;
            }
            close(pipe1[READ_END]);

            // 发送回复给父进程
            write(pipe2[WRITE_END], child_msg, strlen(child_msg) + 1);
            close(pipe2[WRITE_END]);

            std::cout << "子进程发送：" << child_msg << std::endl;
        }
    }

    // 示例3：演示阻塞行为
    void blockingExample() {
        std::cout << "\n=== 示例3：管道阻塞行为演示 ===" << std::endl;

        int fd[2];

        if (pipe(fd) == -1) {
            perror("pipe creation failed");
            return;
        }

        pid_t pid = fork();

        if (pid > 0) {
            // === 父进程 ===
            close(fd[READ_END]);

            std::cout << "父进程：等待3秒后写入数据..." << std::endl;
            sleep(3);  // 让子进程先开始读取（演示阻塞）

            char msg[] = "Delayed message";
            write(fd[WRITE_END], msg, strlen(msg) + 1);
            std::cout << "父进程：数据已写入" << std::endl;

            close(fd[WRITE_END]);
            wait(nullptr);

        } else {
            // === 子进程 ===
            close(fd[WRITE_END]);

            std::cout << "子进程：开始读取数据（可能会阻塞）..." << std::endl;

            char buffer[BUFFER_SIZE];
            ssize_t bytes_read = read(fd[READ_END], buffer, BUFFER_SIZE);

            if (bytes_read > 0) {
                std::cout << "子进程：读取成功：" << buffer << std::endl;
            }

            close(fd[READ_END]);
        }
    }

    // 示例4：错误处理和边界情况
    void errorHandlingExample() {
        std::cout << "\n=== 示例4：错误处理和边界情况 ===" << std::endl;

        int fd[2];

        if (pipe(fd) == -1) {
            perror("pipe creation failed");
            return;
        }

        pid_t pid = fork();

        if (pid > 0) {
            // === 父进程 ===
            close(fd[READ_END]);

            // 写入大量数据测试管道容量
            const char* large_msg =
                "This is a test message that we'll write multiple times to "
                "test pipe capacity. ";
            int write_count = 0;

            for (int i = 0; i < 100; i++) {
                ssize_t result =
                    write(fd[WRITE_END], large_msg, strlen(large_msg));
                if (result == -1) {
                    if (errno == EPIPE) {
                        std::cout << "父进程：检测到SIGPIPE（读端已关闭）"
                                  << std::endl;
                    } else {
                        perror("write error");
                    }
                    break;
                } else {
                    write_count++;
                }
            }

            std::cout << "父进程：成功写入 " << write_count << " 次"
                      << std::endl;
            close(fd[WRITE_END]);
            wait(nullptr);

        } else {
            // === 子进程 ===
            close(fd[WRITE_END]);

            char buffer[BUFFER_SIZE];
            int read_count = 0;
            ssize_t total_bytes = 0;

            // 读取所有数据
            while (true) {
                ssize_t bytes_read = read(fd[READ_END], buffer, BUFFER_SIZE);

                if (bytes_read == -1) {
                    perror("read error");
                    break;
                } else if (bytes_read == 0) {
                    std::cout << "子进程：检测到EOF，读取结束" << std::endl;
                    break;
                } else {
                    read_count++;
                    total_bytes += bytes_read;
                }

                // 模拟读取延迟
                if (read_count == 5) {
                    std::cout << "子进程：读取5次后暂停..." << std::endl;
                    sleep(1);
                }
            }

            std::cout << "子进程：总共读取 " << read_count << " 次，"
                      << total_bytes << " 字节" << std::endl;

            close(fd[READ_END]);
        }
    }

    void task() {
        std::cout << "=== PIPE 管道通信教学示例 ===" << std::endl;

        basicExample();          // 基础示例
        bidirectionalExample();  // 双向通信
        blockingExample();       // 阻塞行为
        errorHandlingExample();  // 错误处理
    }
}  // namespace PIPE

namespace FIFO {
    /*
    ====================================================================================================
                                        FIFO (命名管道) 教学文档
    ====================================================================================================
    
    1. 什么是FIFO？
       - FIFO（First In First Out）也称为命名管道（Named Pipe）
       - 与匿名管道不同，FIFO在文件系统中有名字
       - 可以在无亲缘关系的进程间通信
    
    2. FIFO特点：
       - 存在于文件系统中（通常在/tmp目录）
       - 半双工通信
       - 具有文件的访问权限
       - 进程结束后FIFO文件仍然存在
    
    3. 核心系统调用：
       - mkfifo()：创建FIFO
       - open()：打开FIFO
       - read()/write()：读写数据
       - unlink()：删除FIFO文件
    ====================================================================================================
    */

    // FIFO 写入者
    void writer() {
        std::cout << "\n=== FIFO 写入者进程 ===" << std::endl;

        // 创建FIFO（如果不存在）
        if (mkfifo(FIFO_NAME, 0666) == -1) {
            if (errno != EEXIST) {
                perror("mkfifo failed");
                return;
            }
        }

        std::cout << "正在打开FIFO进行写入..." << std::endl;

        // 打开FIFO进行写入
        int fd = open(FIFO_NAME, O_WRONLY);
        if (fd == -1) {
            perror("open FIFO for writing failed");
            return;
        }

        // 写入数据
        const char* messages[] = {"Hello from FIFO writer!",
                                  "This is message 2", "This is message 3",
                                  "Goodbye!"};

        for (int i = 0; i < 4; i++) {
            ssize_t bytes_written =
                write(fd, messages[i], strlen(messages[i]) + 1);
            if (bytes_written == -1) {
                perror("write to FIFO failed");
                break;
            }
            std::cout << "写入消息 " << (i + 1) << ": " << messages[i]
                      << std::endl;
            sleep(1);  // 延迟以观察效果
        }

        close(fd);
        std::cout << "FIFO写入者完成" << std::endl;
    }

    // FIFO 读取者
    void reader() {
        std::cout << "\n=== FIFO 读取者进程 ===" << std::endl;

        std::cout << "正在打开FIFO进行读取..." << std::endl;

        // 打开FIFO进行读取
        int fd = open(FIFO_NAME, O_RDONLY);
        if (fd == -1) {
            perror("open FIFO for reading failed");
            return;
        }

        char buffer[BUFFER_SIZE];
        int message_count = 0;

        // 读取数据
        while (true) {
            ssize_t bytes_read = read(fd, buffer, BUFFER_SIZE);

            if (bytes_read == -1) {
                perror("read from FIFO failed");
                break;
            } else if (bytes_read == 0) {
                std::cout << "检测到EOF，写入者已关闭FIFO" << std::endl;
                break;
            } else {
                message_count++;
                std::cout << "读取消息 " << message_count << ": " << buffer
                          << std::endl;
            }
        }

        close(fd);
        std::cout << "FIFO读取者完成" << std::endl;
    }

    // 演示FIFO通信
    void demonstrateFIFO() {
        std::cout << "\n=== FIFO 命名管道演示 ===" << std::endl;

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            return;
        }

        if (pid > 0) {
            // 父进程作为写入者
            sleep(1);  // 让子进程先启动
            writer();
            wait(nullptr);

            // 清理FIFO文件
            if (unlink(FIFO_NAME) == -1) {
                perror("unlink FIFO failed");
            } else {
                std::cout << "FIFO文件已删除" << std::endl;
            }
        } else {
            // 子进程作为读取者
            reader();
        }
    }

    void task() { demonstrateFIFO(); }
}  // namespace FIFO

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "      进程间通信 - 管道教学示例" << std::endl;
    std::cout << "========================================" << std::endl;

    // 运行PIPE示例
    PIPE::task();

    std::cout << "\n\n";

    // 运行FIFO示例
    FIFO::task();

    return 0;
}