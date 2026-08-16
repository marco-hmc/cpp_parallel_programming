#include <errno.h>
#include <sys/ipc.h>
#include <sys/msg.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <iostream>

/*
====================================================================================================
                                    消息队列 (Message Queue) 教学文档
====================================================================================================

1. 什么是消息队列？
   - 消息队列是一种进程间通信（IPC）机制
   - 允许进程以消息的形式交换数据
   - 消息在内核中排队，按照FIFO或优先级顺序处理
   - 与管道不同，消息队列是面向消息的，而不是字节流

2. 消息队列的特点：
   - 异步通信：发送方和接收方不需要同时运行
   - 消息边界：每个消息都有明确的边界
   - 消息类型：可以给消息指定类型，接收方可以选择性接收
   - 持久性：消息队列在进程结束后仍然存在，直到被显式删除
   - 容量限制：系统对消息队列的大小和数量有限制

3. 核心系统调用：
   - msgget()：创建或获取消息队列
   - msgsnd()：发送消息
   - msgrcv()：接收消息
   - msgctl()：控制消息队列（删除、查询状态等）

4. 消息结构：
   struct msgbuf {
       long mtype;     // 消息类型（必须 > 0）
       char mtext[1];  // 消息内容（可变长度）
   };

5. 重要概念：
   - 消息队列标识符（msgqid）：内核分配的唯一标识符
   - 键值（key）：用于标识消息队列的键，可以是IPC_PRIVATE或ftok()生成
   - 消息类型（mtype）：用于消息的分类和选择性接收
   - 阻塞/非阻塞：可以设置发送和接收操作的阻塞模式

====================================================================================================
*/

namespace MessageQueue {

    // 定义消息结构
    struct Message {
        long mtype;       // 消息类型
        char mtext[256];  // 消息内容
    };

    // 消息类型常量
    const long MSG_TYPE_REQUEST = 1;
    const long MSG_TYPE_RESPONSE = 2;
    const long MSG_TYPE_NOTIFICATION = 3;
    const long MSG_TYPE_PRIORITY = 10;

    // 示例1：基础消息队列通信
    void basicExample() {
        std::cout << "\n=== 示例1：基础消息队列通信 ===" << std::endl;

        // 步骤1：创建或获取消息队列
        key_t key = ftok(".", 'A');  // 使用当前目录和字符'A'生成键值
        if (key == -1) {
            perror("ftok failed");
            return;
        }

        int msgqid = msgget(key, IPC_CREAT | 0666);
        if (msgqid == -1) {
            perror("msgget failed");
            return;
        }

        std::cout << "消息队列创建成功，ID=" << msgqid << "，键值=" << key
                  << std::endl;

        // 步骤2：创建子进程
        pid_t pid = fork();

        if (pid < 0) {
            perror("fork failed");
            return;
        }

        if (pid > 0) {
            // === 父进程（发送者） ===
            std::cout << "父进程 PID=" << getpid() << " 开始发送消息"
                      << std::endl;

            Message msg;
            msg.mtype = MSG_TYPE_REQUEST;
            strcpy(msg.mtext, "Hello from parent process!");

            // 发送消息
            if (msgsnd(msgqid, &msg, strlen(msg.mtext) + 1, 0) == -1) {
                perror("msgsnd failed");
            } else {
                std::cout << "父进程发送消息：类型=" << msg.mtype << "，内容=\""
                          << msg.mtext << "\"" << std::endl;
            }

            // 等待子进程结束
            wait(nullptr);

            // 清理消息队列
            if (msgctl(msgqid, IPC_RMID, nullptr) == -1) {
                perror("msgctl IPC_RMID failed");
            } else {
                std::cout << "消息队列已删除" << std::endl;
            }

        } else {
            // === 子进程（接收者） ===
            std::cout << "子进程 PID=" << getpid() << " 开始接收消息"
                      << std::endl;

            Message msg;

            // 接收指定类型的消息
            ssize_t msg_size =
                msgrcv(msgqid, &msg, sizeof(msg.mtext), MSG_TYPE_REQUEST, 0);

            if (msg_size == -1) {
                perror("msgrcv failed");
            } else {
                std::cout << "子进程接收消息：类型=" << msg.mtype
                          << "，大小=" << msg_size << "，内容=\"" << msg.mtext
                          << "\"" << std::endl;
            }
        }
    }

    // 示例2：双向通信和消息类型
    void bidirectionalExample() {
        std::cout << "\n=== 示例2：双向通信和消息类型 ===" << std::endl;

        key_t key = ftok(".", 'B');
        int msgqid = msgget(key, IPC_CREAT | 0666);

        if (msgqid == -1) {
            perror("msgget failed");
            return;
        }

        pid_t pid = fork();

        if (pid > 0) {
            // === 父进程（服务端） ===
            std::cout << "父进程作为服务端启动" << std::endl;

            Message request, response;

            // 接收请求消息
            ssize_t size = msgrcv(msgqid, &request, sizeof(request.mtext),
                                  MSG_TYPE_REQUEST, 0);
            if (size > 0) {
                std::cout << "服务端收到请求：" << request.mtext << std::endl;

                // 发送响应消息
                response.mtype = MSG_TYPE_RESPONSE;
                strcpy(response.mtext,
                       "Response: Request processed successfully!");

                if (msgsnd(msgqid, &response, strlen(response.mtext) + 1, 0) ==
                    0) {
                    std::cout << "服务端发送响应：" << response.mtext
                              << std::endl;
                }
            }

            wait(nullptr);
            msgctl(msgqid, IPC_RMID, nullptr);

        } else {
            // === 子进程（客户端） ===
            std::cout << "子进程作为客户端启动" << std::endl;

            Message request, response;

            // 发送请求消息
            request.mtype = MSG_TYPE_REQUEST;
            strcpy(request.mtext, "Request: Please process this data");

            if (msgsnd(msgqid, &request, strlen(request.mtext) + 1, 0) == 0) {
                std::cout << "客户端发送请求：" << request.mtext << std::endl;
            }

            // 接收响应消息
            ssize_t size = msgrcv(msgqid, &response, sizeof(response.mtext),
                                  MSG_TYPE_RESPONSE, 0);
            if (size > 0) {
                std::cout << "客户端收到响应：" << response.mtext << std::endl;
            }
        }
    }

    // 示例3：消息优先级和选择性接收
    void priorityExample() {
        std::cout << "\n=== 示例3：消息优先级和选择性接收 ===" << std::endl;

        key_t key = ftok(".", 'C');
        int msgqid = msgget(key, IPC_CREAT | 0666);

        if (msgqid == -1) {
            perror("msgget failed");
            return;
        }

        pid_t pid = fork();

        if (pid > 0) {
            // === 父进程（发送多种类型的消息） ===
            std::cout << "父进程发送不同优先级的消息" << std::endl;

            Message msg;

            // 发送普通消息
            msg.mtype = MSG_TYPE_NOTIFICATION;
            strcpy(msg.mtext, "Normal notification message");
            msgsnd(msgqid, &msg, strlen(msg.mtext) + 1, 0);
            std::cout << "发送普通消息（类型" << msg.mtype << "）" << std::endl;

            // 发送高优先级消息
            msg.mtype = MSG_TYPE_PRIORITY;
            strcpy(msg.mtext, "High priority urgent message!");
            msgsnd(msgqid, &msg, strlen(msg.mtext) + 1, 0);
            std::cout << "发送高优先级消息（类型" << msg.mtype << "）"
                      << std::endl;

            // 发送另一个普通消息
            msg.mtype = MSG_TYPE_NOTIFICATION;
            strcpy(msg.mtext, "Another normal message");
            msgsnd(msgqid, &msg, strlen(msg.mtext) + 1, 0);
            std::cout << "发送另一个普通消息（类型" << msg.mtype << "）"
                      << std::endl;

            wait(nullptr);
            msgctl(msgqid, IPC_RMID, nullptr);

        } else {
            // === 子进程（优先接收高优先级消息） ===
            std::cout << "子进程开始接收消息（优先处理高优先级）" << std::endl;

            Message msg;

            // 先接收高优先级消息
            std::cout << "\n1. 优先接收高优先级消息：" << std::endl;
            ssize_t size =
                msgrcv(msgqid, &msg, sizeof(msg.mtext), MSG_TYPE_PRIORITY, 0);
            if (size > 0) {
                std::cout << "   接收到优先级消息：" << msg.mtext << std::endl;
            }

            // 然后接收普通消息
            std::cout << "\n2. 接收普通消息：" << std::endl;
            while (true) {
                size = msgrcv(msgqid, &msg, sizeof(msg.mtext),
                              MSG_TYPE_NOTIFICATION, IPC_NOWAIT);
                if (size == -1) {
                    if (errno == ENOMSG) {
                        std::cout << "   没有更多普通消息" << std::endl;
                        break;
                    } else {
                        perror("msgrcv failed");
                        break;
                    }
                } else {
                    std::cout << "   接收到普通消息：" << msg.mtext
                              << std::endl;
                }
            }
        }
    }

    // 示例4：非阻塞模式和错误处理
    void nonBlockingExample() {
        std::cout << "\n=== 示例4：非阻塞模式和错误处理 ===" << std::endl;

        key_t key = ftok(".", 'D');
        int msgqid = msgget(key, IPC_CREAT | 0666);

        if (msgqid == -1) {
            perror("msgget failed");
            return;
        }

        pid_t pid = fork();

        if (pid > 0) {
            // === 父进程 ===
            std::cout << "父进程：等待3秒后发送消息" << std::endl;
            sleep(3);

            Message msg;
            msg.mtype = MSG_TYPE_NOTIFICATION;
            strcpy(msg.mtext, "Delayed message");

            if (msgsnd(msgqid, &msg, strlen(msg.mtext) + 1, 0) == 0) {
                std::cout << "父进程：消息发送成功" << std::endl;
            }

            wait(nullptr);
            msgctl(msgqid, IPC_RMID, nullptr);

        } else {
            // === 子进程（演示非阻塞接收） ===
            std::cout << "子进程：尝试非阻塞接收消息" << std::endl;

            Message msg;

            // 非阻塞尝试接收消息
            for (int i = 0; i < 5; i++) {
                ssize_t size = msgrcv(msgqid, &msg, sizeof(msg.mtext),
                                      MSG_TYPE_NOTIFICATION, IPC_NOWAIT);

                if (size == -1) {
                    if (errno == ENOMSG) {
                        std::cout << "尝试 " << (i + 1)
                                  << ": 暂无消息，继续等待..." << std::endl;
                        sleep(1);
                    } else {
                        perror("msgrcv failed");
                        break;
                    }
                } else {
                    std::cout << "接收到消息：" << msg.mtext << std::endl;
                    break;
                }
            }

            // 最后一次阻塞接收
            std::cout << "切换到阻塞模式接收..." << std::endl;
            ssize_t size = msgrcv(msgqid, &msg, sizeof(msg.mtext),
                                  MSG_TYPE_NOTIFICATION, 0);
            if (size > 0) {
                std::cout << "阻塞模式接收成功：" << msg.mtext << std::endl;
            }
        }
    }

    // 示例5：查询消息队列状态
    void statusExample() {
        std::cout << "\n=== 示例5：查询消息队列状态 ===" << std::endl;

        key_t key = ftok(".", 'E');
        int msgqid = msgget(key, IPC_CREAT | 0666);

        if (msgqid == -1) {
            perror("msgget failed");
            return;
        }

        // 发送几条消息
        Message msg;
        for (int i = 1; i <= 3; i++) {
            msg.mtype = i;
            snprintf(msg.mtext, sizeof(msg.mtext), "Test message %d", i);
            msgsnd(msgqid, &msg, strlen(msg.mtext) + 1, 0);
        }

        // 查询消息队列状态
        struct msqid_ds buf;
        if (msgctl(msgqid, IPC_STAT, &buf) == 0) {
            std::cout << "消息队列状态信息：" << std::endl;
            std::cout << "  当前消息数量：" << buf.msg_qnum << std::endl;
            std::cout << "  最大字节数：" << buf.msg_qbytes << std::endl;
            std::cout << "  最后发送PID：" << buf.msg_lspid << std::endl;
            std::cout << "  最后接收PID：" << buf.msg_lrpid << std::endl;
        }

        // 清理：接收所有消息
        std::cout << "\n清理消息队列：" << std::endl;
        while (msgrcv(msgqid, &msg, sizeof(msg.mtext), 0, IPC_NOWAIT) != -1) {
            std::cout << "  清理消息：类型=" << msg.mtype
                      << "，内容=" << msg.mtext << std::endl;
        }

        msgctl(msgqid, IPC_RMID, nullptr);
        std::cout << "消息队列已删除" << std::endl;
    }

    void task() {
        std::cout << "=== 消息队列 (Message Queue) 教学示例 ===" << std::endl;

        basicExample();          // 基础示例
        bidirectionalExample();  // 双向通信
        priorityExample();       // 消息优先级
        nonBlockingExample();    // 非阻塞模式
        statusExample();         // 状态查询
    }

}  // namespace MessageQueue

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "      进程间通信 - 消息队列教学示例" << std::endl;
    std::cout << "========================================" << std::endl;

    MessageQueue::task();

    std::cout << "\n注意事项：" << std::endl;
    std::cout << "1. 消息队列具有持久性，程序结束后仍存在" << std::endl;
    std::cout << "2. 可以使用 'ipcs -q' 命令查看系统中的消息队列" << std::endl;
    std::cout << "3. 可以使用 'ipcrm -q <msgqid>' 命令手动删除消息队列"
              << std::endl;
    std::cout << "4. 系统对消息队列的数量和大小有限制" << std::endl;

    return 0;
}