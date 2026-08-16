#include <errno.h>
#include <fcntl.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cstring>
#include <iostream>
#include <string>

/*
====================================================================================================
                                共享内存 (Shared Memory) 教学文档
====================================================================================================

1. 什么是共享内存？
   - 共享内存是最快的IPC机制，允许多个进程访问同一块物理内存区域
   - 进程可以直接读写共享内存，避免了数据拷贝的开销
   - 共享内存本身不提供同步机制，需要配合信号量、互斥锁等同步原语使用

2. 共享内存的特点：
   - 高效性：避免了内核态和用户态之间的数据拷贝
   - 易用性：可以像访问普通内存一样访问共享内存
   - 持久性：共享内存段在创建后会一直存在，直到被显式删除
   - 同步问题：需要额外的同步机制来避免竞态条件

3. System V 共享内存 vs mmap：
   - System V：传统的共享内存API，使用键值标识
   - mmap：更现代的内存映射方式，可以映射文件或匿名内存

4. 核心系统调用：
   - shmget()：创建或获取共享内存段
   - shmat()：将共享内存附加到进程地址空间
   - shmdt()：从进程地址空间分离共享内存
   - shmctl()：控制共享内存段
   - mmap()/munmap()：内存映射和解除映射

====================================================================================================
*/

namespace SharedMemory {

    // 共享数据结构示例
    struct SharedData {
        int counter;
        char message[256];
        bool ready;
        pid_t writer_pid;
    };

    // 示例1：基础的System V共享内存
    void systemVBasicExample() {
        std::cout << "\n=== 示例1：基础的System V共享内存 ===" << std::endl;

        // 创建共享内存段
        int shmid = shmget(IPC_PRIVATE, sizeof(SharedData), IPC_CREAT | 0666);
        if (shmid == -1) {
            perror("shmget failed");
            return;
        }

        std::cout << "共享内存ID: " << shmid << std::endl;
        std::cout << "共享内存大小: " << sizeof(SharedData) << " 字节"
                  << std::endl;

        // 附加共享内存到进程地址空间
        SharedData* sharedData = (SharedData*)shmat(shmid, nullptr, 0);
        if (sharedData == (SharedData*)-1) {
            perror("shmat failed");
            shmctl(shmid, IPC_RMID, nullptr);
            return;
        }

        std::cout << "共享内存地址: " << sharedData << std::endl;

        // 初始化共享数据
        sharedData->counter = 0;
        strcpy(sharedData->message, "Hello from parent!");
        sharedData->ready = false;
        sharedData->writer_pid = getpid();

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
        } else if (pid == 0) {
            // === 子进程 ===
            std::cout << "子进程启动，PID: " << getpid() << std::endl;

            // 子进程也需要附加共享内存
            SharedData* childSharedData = (SharedData*)shmat(shmid, nullptr, 0);
            if (childSharedData == (SharedData*)-1) {
                perror("child shmat failed");
                exit(1);
            }

            // 读取父进程写入的数据
            std::cout << "子进程读取: counter=" << childSharedData->counter
                      << ", message=" << childSharedData->message << std::endl;

            // 修改共享数据
            childSharedData->counter = 42;
            strcpy(childSharedData->message, "Hello from child!");
            childSharedData->ready = true;
            childSharedData->writer_pid = getpid();

            std::cout << "子进程修改了共享数据" << std::endl;

            // 分离共享内存
            if (shmdt(childSharedData) == -1) {
                perror("child shmdt failed");
            }

            exit(0);
        } else {
            // === 父进程 ===
            // 等待子进程修改数据
            sleep(1);

            // 读取子进程的修改
            std::cout << "父进程读取子进程的修改: counter="
                      << sharedData->counter
                      << ", message=" << sharedData->message
                      << ", writer_pid=" << sharedData->writer_pid << std::endl;

            // 等待子进程结束
            wait(nullptr);
        }

        // 分离共享内存
        if (shmdt(sharedData) == -1) {
            perror("shmdt failed");
        }

        // 删除共享内存段
        if (shmctl(shmid, IPC_RMID, nullptr) == -1) {
            perror("shmctl IPC_RMID failed");
        }

        std::cout << "共享内存已清理" << std::endl;
    }

    // 示例2：使用ftok生成键值的共享内存
    void systemVWithKeyExample() {
        std::cout << "\n=== 示例2：使用ftok生成键值的共享内存 ===" << std::endl;

        // 使用当前程序文件生成键值
        key_t key = ftok(".", 'A');
        if (key == -1) {
            perror("ftok failed");
            return;
        }

        std::cout << "生成的键值: 0x" << std::hex << key << std::dec
                  << std::endl;

        // 创建或获取共享内存段
        int shmid = shmget(key, sizeof(int), IPC_CREAT | 0666);
        if (shmid == -1) {
            perror("shmget failed");
            return;
        }

        int* counter = (int*)shmat(shmid, nullptr, 0);
        if (counter == (int*)-1) {
            perror("shmat failed");
            shmctl(shmid, IPC_RMID, nullptr);
            return;
        }

        // 如果是第一次创建，初始化计数器
        struct shmid_ds shm_info;
        if (shmctl(shmid, IPC_STAT, &shm_info) == 0) {
            std::cout << "共享内存信息:" << std::endl;
            std::cout << "  大小: " << shm_info.shm_segsz << " 字节"
                      << std::endl;
            std::cout << "  附加进程数: " << shm_info.shm_nattch << std::endl;
            std::cout << "  创建时间: " << ctime(&shm_info.shm_ctime);
        }

        // 原子地增加计数器
        (*counter)++;
        std::cout << "当前计数器值: " << *counter << std::endl;

        // 分离共享内存
        shmdt(counter);

        // 注意：这里不删除共享内存，让多个进程可以共享
        std::cout << "使用 'ipcs -m' 查看系统中的共享内存段" << std::endl;
        std::cout << "使用 'ipcrm -m " << shmid << "' 手动删除该共享内存段"
                  << std::endl;
    }

    // 示例3：生产者-消费者模式
    struct CircularBuffer {
        static const int BUFFER_SIZE = 10;
        int buffer[BUFFER_SIZE];
        int head;   // 生产者写入位置
        int tail;   // 消费者读取位置
        int count;  // 当前元素数量
        bool producer_done;
    };

    void producerConsumerExample() {
        std::cout << "\n=== 示例3：生产者-消费者模式 ===" << std::endl;

        int shmid =
            shmget(IPC_PRIVATE, sizeof(CircularBuffer), IPC_CREAT | 0666);
        if (shmid == -1) {
            perror("shmget failed");
            return;
        }

        CircularBuffer* buffer = (CircularBuffer*)shmat(shmid, nullptr, 0);
        if (buffer == (CircularBuffer*)-1) {
            perror("shmat failed");
            shmctl(shmid, IPC_RMID, nullptr);
            return;
        }

        // 初始化缓冲区
        buffer->head = 0;
        buffer->tail = 0;
        buffer->count = 0;
        buffer->producer_done = false;

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
        } else if (pid == 0) {
            // === 消费者进程 ===
            std::cout << "消费者进程启动" << std::endl;

            CircularBuffer* consumerBuffer =
                (CircularBuffer*)shmat(shmid, nullptr, 0);
            if (consumerBuffer == (CircularBuffer*)-1) {
                perror("consumer shmat failed");
                exit(1);
            }

            int consumed = 0;
            while (!consumerBuffer->producer_done ||
                   consumerBuffer->count > 0) {
                if (consumerBuffer->count > 0) {
                    // 消费数据
                    int item = consumerBuffer->buffer[consumerBuffer->tail];
                    consumerBuffer->tail = (consumerBuffer->tail + 1) %
                                           CircularBuffer::BUFFER_SIZE;
                    consumerBuffer->count--;

                    std::cout << "消费者消费: " << item
                              << " (剩余: " << consumerBuffer->count << ")"
                              << std::endl;
                    consumed++;

                    usleep(100000);  // 模拟处理时间
                } else {
                    usleep(10000);  // 等待数据
                }
            }

            std::cout << "消费者完成，总共消费: " << consumed << " 个项目"
                      << std::endl;
            shmdt(consumerBuffer);
            exit(0);

        } else {
            // === 生产者进程 ===
            std::cout << "生产者进程启动" << std::endl;

            for (int i = 1; i <= 20; i++) {
                // 等待缓冲区有空间
                while (buffer->count >= CircularBuffer::BUFFER_SIZE) {
                    usleep(10000);
                }

                // 生产数据
                buffer->buffer[buffer->head] = i;
                buffer->head = (buffer->head + 1) % CircularBuffer::BUFFER_SIZE;
                buffer->count++;

                std::cout << "生产者生产: " << i
                          << " (缓冲区: " << buffer->count << "/"
                          << CircularBuffer::BUFFER_SIZE << ")" << std::endl;

                usleep(50000);  // 模拟生产时间
            }

            buffer->producer_done = true;
            std::cout << "生产者完成" << std::endl;

            // 等待消费者完成
            wait(nullptr);
        }

        shmdt(buffer);
        shmctl(shmid, IPC_RMID, nullptr);
        std::cout << "生产者-消费者示例完成" << std::endl;
    }

    // 示例4：使用mmap实现共享内存
    void mmapExample() {
        std::cout << "\n=== 示例4：使用mmap实现共享内存 ===" << std::endl;

        // 创建临时文件
        const char* filename = "/tmp/mmap_shared_file";
        int fd = open(filename, O_CREAT | O_RDWR | O_TRUNC, 0666);
        if (fd == -1) {
            perror("open failed");
            return;
        }

        // 设置文件大小
        size_t size = sizeof(SharedData);
        if (ftruncate(fd, size) == -1) {
            perror("ftruncate failed");
            close(fd);
            unlink(filename);
            return;
        }

        // 映射文件到内存
        SharedData* mappedData = (SharedData*)mmap(
            nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (mappedData == MAP_FAILED) {
            perror("mmap failed");
            close(fd);
            unlink(filename);
            return;
        }

        std::cout << "mmap映射地址: " << mappedData << std::endl;
        std::cout << "映射文件: " << filename << std::endl;

        // 初始化数据
        mappedData->counter = 100;
        strcpy(mappedData->message, "mmap shared memory");
        mappedData->ready = false;
        mappedData->writer_pid = getpid();

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
        } else if (pid == 0) {
            // === 子进程 ===
            std::cout << "子进程使用继承的mmap映射" << std::endl;

            // 读取父进程的数据
            std::cout << "子进程读取: counter=" << mappedData->counter
                      << ", message=" << mappedData->message << std::endl;

            // 修改数据
            mappedData->counter += 50;
            strcat(mappedData->message, " - modified by child");
            mappedData->ready = true;
            mappedData->writer_pid = getpid();

            // 确保数据写入文件
            if (msync(mappedData, size, MS_SYNC) == -1) {
                perror("msync failed");
            }

            std::cout << "子进程修改完成并同步到文件" << std::endl;
            exit(0);

        } else {
            // === 父进程 ===
            sleep(1);  // 等待子进程修改

            std::cout << "父进程读取修改后的数据: counter="
                      << mappedData->counter
                      << ", message=" << mappedData->message << std::endl;

            wait(nullptr);
        }

        // 解除映射
        if (munmap(mappedData, size) == -1) {
            perror("munmap failed");
        }

        close(fd);
        unlink(filename);  // 删除临时文件

        std::cout << "mmap示例完成" << std::endl;
    }

    // 示例5：匿名mmap共享内存
    void anonymousMmapExample() {
        std::cout << "\n=== 示例5：匿名mmap共享内存 ===" << std::endl;

        size_t size = sizeof(SharedData);

        // 创建匿名共享内存映射
        SharedData* sharedData =
            (SharedData*)mmap(nullptr, size, PROT_READ | PROT_WRITE,
                              MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (sharedData == MAP_FAILED) {
            perror("anonymous mmap failed");
            return;
        }

        std::cout << "匿名mmap地址: " << sharedData << std::endl;

        // 初始化数据
        sharedData->counter = 200;
        strcpy(sharedData->message, "anonymous mmap");
        sharedData->ready = false;
        sharedData->writer_pid = getpid();

        pid_t pid = fork();
        if (pid == -1) {
            perror("fork failed");
        } else if (pid == 0) {
            // === 子进程 ===
            std::cout << "子进程访问匿名共享内存" << std::endl;

            std::cout << "子进程读取: counter=" << sharedData->counter
                      << std::endl;

            // 修改计数器
            for (int i = 0; i < 5; i++) {
                sharedData->counter++;
                std::cout << "子进程递增计数器: " << sharedData->counter
                          << std::endl;
                usleep(100000);
            }

            sharedData->ready = true;
            exit(0);

        } else {
            // === 父进程 ===
            // 等待子进程开始工作
            sleep(1);

            // 监控计数器变化
            int lastCounter = sharedData->counter;
            while (!sharedData->ready) {
                if (sharedData->counter != lastCounter) {
                    std::cout << "父进程观察到计数器变化: " << lastCounter
                              << " -> " << sharedData->counter << std::endl;
                    lastCounter = sharedData->counter;
                }
                usleep(50000);
            }

            wait(nullptr);
            std::cout << "最终计数器值: " << sharedData->counter << std::endl;
        }

        // 解除映射
        if (munmap(sharedData, size) == -1) {
            perror("munmap failed");
        }

        std::cout << "匿名mmap示例完成" << std::endl;
    }

    // 示例6：共享内存的错误处理和最佳实践
    void errorHandlingExample() {
        std::cout << "\n=== 示例6：错误处理和最佳实践 ===" << std::endl;

        // 1. 检查共享内存是否已存在
        key_t key = ftok(".", 'B');
        if (key == -1) {
            perror("ftok failed");
            return;
        }

        // 尝试获取已存在的共享内存
        int shmid = shmget(key, 0, 0);
        if (shmid != -1) {
            std::cout << "警告: 共享内存段已存在 (ID: " << shmid << ")"
                      << std::endl;

            // 获取信息并清理
            struct shmid_ds shm_info;
            if (shmctl(shmid, IPC_STAT, &shm_info) == 0) {
                std::cout << "现有共享内存信息:" << std::endl;
                std::cout << "  附加进程数: " << shm_info.shm_nattch
                          << std::endl;

                if (shm_info.shm_nattch == 0) {
                    std::cout << "清理孤立的共享内存段" << std::endl;
                    shmctl(shmid, IPC_RMID, nullptr);
                }
            }
        }

        // 2. 使用IPC_EXCL确保创建新的共享内存
        shmid = shmget(key, sizeof(int), IPC_CREAT | IPC_EXCL | 0666);
        if (shmid == -1) {
            if (errno == EEXIST) {
                std::cout << "共享内存已存在，获取现有的" << std::endl;
                shmid = shmget(key, sizeof(int), 0666);
            } else {
                perror("shmget with IPC_EXCL failed");
                return;
            }
        }

        if (shmid == -1) {
            perror("shmget failed completely");
            return;
        }

        // 3. 安全地附加和使用共享内存
        int* counter = (int*)shmat(shmid, nullptr, 0);
        if (counter == (int*)-1) {
            perror("shmat failed");
            shmctl(shmid, IPC_RMID, nullptr);
            return;
        }

        // 4. 使用内存屏障确保数据一致性（在多线程环境中）
        *counter = 0;
        __sync_synchronize();  // 内存屏障

        std::cout << "安全地初始化计数器: " << *counter << std::endl;

        // 5. 正确的清理顺序
        if (shmdt(counter) == -1) {
            perror("shmdt failed");
        }

        // 获取当前附加进程数
        struct shmid_ds shm_info;
        if (shmctl(shmid, IPC_STAT, &shm_info) == 0) {
            std::cout << "分离后附加进程数: " << shm_info.shm_nattch
                      << std::endl;

            // 只有当没有进程附加时才删除
            if (shm_info.shm_nattch == 0) {
                if (shmctl(shmid, IPC_RMID, nullptr) == -1) {
                    perror("shmctl IPC_RMID failed");
                } else {
                    std::cout << "共享内存段已删除" << std::endl;
                }
            }
        }

        std::cout << "错误处理示例完成" << std::endl;
    }

    void task() {
        std::cout << "=== 共享内存 (Shared Memory) 教学示例 ===" << std::endl;

        systemVBasicExample();      // 基础System V共享内存
        systemVWithKeyExample();    // 使用ftok的共享内存
        producerConsumerExample();  // 生产者-消费者模式
        mmapExample();              // 文件映射共享内存
        anonymousMmapExample();     // 匿名映射共享内存
        errorHandlingExample();     // 错误处理和最佳实践
    }

}  // namespace SharedMemory

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "     进程间通信 - 共享内存教学示例" << std::endl;
    std::cout << "========================================" << std::endl;

    SharedMemory::task();

    std::cout << "\n重要提示：" << std::endl;
    std::cout << "1. 共享内存本身不提供同步机制，需要配合信号量等使用"
              << std::endl;
    std::cout << "2. 记得正确分离和删除共享内存段，避免资源泄漏" << std::endl;
    std::cout << "3. 使用 'ipcs -m' 查看系统中的共享内存段" << std::endl;
    std::cout << "4. 使用 'ipcrm -m <shmid>' 删除孤立的共享内存段" << std::endl;
    std::cout << "5. mmap方式更现代，但System V方式兼容性更好" << std::endl;
    std::cout << "6. 在实际应用中考虑使用POSIX共享内存(shm_open)" << std::endl;

    return 0;
}
