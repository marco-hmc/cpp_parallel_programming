#include <errno.h>
#include <fcntl.h>
#include <semaphore.h>
#include <sys/ipc.h>
#include <sys/mman.h>
#include <sys/sem.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <thread>

/*
====================================================================================================
                                信号量 (Semaphore) 教学文档
====================================================================================================

1. 什么是信号量？
   - 信号量是一种同步原语，用于控制对共享资源的访问
   - 由荷兰计算机科学家Dijkstra提出，用于解决生产者-消费者问题
   - 信号量维护一个非负整数计数器，表示可用资源的数量

2. 信号量的类型：
   - 二进制信号量：计数器只能是0或1，类似于互斥锁
   - 计数信号量：计数器可以是任意非负整数，用于控制资源池

3. 信号量的操作：
   - P操作(wait/down)：减少信号量的值，如果值为0则阻塞
   - V操作(signal/up)：增加信号量的值，可能唤醒等待的进程

4. 两种信号量实现：
   - System V信号量：传统的Unix信号量，支持信号量集合
   - POSIX信号量：更现代的实现，分为命名和匿名两种

5. 核心系统调用：
   System V:
   - semget()：创建或获取信号量集合
   - semctl()：控制信号量集合
   - semop()：执行信号量操作
   
   POSIX:
   - sem_open()/sem_close()：命名信号量
   - sem_init()/sem_destroy()：匿名信号量
   - sem_wait()/sem_post()：P/V操作

====================================================================================================
*/

namespace Semaphore {

    // System V信号量的联合体定义（某些系统需要）
    union semun {
        int val;
        struct semid_ds* buf;
        unsigned short* array;
    };

    // 共享数据结构
    struct SharedResource {
        int counter;
        char message[256];
        int reader_count;
        bool writer_active;
    };

    // System V信号量操作辅助函数
    class SystemVSemaphore {
      private:
        int semid;

      public:
        SystemVSemaphore(key_t key, int nsems, int flags) {
            semid = semget(key, nsems, flags);
            if (semid == -1) {
                perror("semget failed");
                throw std::runtime_error("Failed to create/get semaphore");
            }
        }

        ~SystemVSemaphore() {
            // 析构函数中不删除信号量，因为可能被其他进程使用
        }

        bool setValue(int semnum, int value) {
            union semun arg;
            arg.val = value;
            return semctl(semid, semnum, SETVAL, arg) != -1;
        }

        int getValue(int semnum) { return semctl(semid, semnum, GETVAL); }

        bool wait(int semnum) {
            struct sembuf sop;
            sop.sem_num = semnum;
            sop.sem_op = -1;  // P操作
            sop.sem_flg = 0;
            return semop(semid, &sop, 1) != -1;
        }

        bool signal(int semnum) {
            struct sembuf sop;
            sop.sem_num = semnum;
            sop.sem_op = 1;  // V操作
            sop.sem_flg = 0;
            return semop(semid, &sop, 1) != -1;
        }

        bool remove() { return semctl(semid, 0, IPC_RMID) != -1; }

        int getId() const { return semid; }
    };

    // 示例1：基础的System V信号量使用
    void systemVBasicExample() {
        std::cout << "\n=== 示例1：基础的System V信号量使用 ===" << std::endl;

        try {
            // 创建信号量集合（包含1个信号量）
            SystemVSemaphore sem(IPC_PRIVATE, 1, IPC_CREAT | 0666);

            std::cout << "信号量ID: " << sem.getId() << std::endl;

            // 初始化信号量值为1（二进制信号量）
            if (!sem.setValue(0, 1)) {
                perror("Failed to set semaphore value");
                return;
            }

            std::cout << "信号量初始值: " << sem.getValue(0) << std::endl;

            pid_t pid = fork();
            if (pid == -1) {
                perror("fork failed");
                return;
            }

            if (pid == 0) {
                // === 子进程 ===
                std::cout << "子进程尝试获取信号量..." << std::endl;

                if (sem.wait(0)) {
                    std::cout << "子进程获得信号量，进入临界区" << std::endl;
                    std::cout << "子进程在临界区工作..." << std::endl;
                    sleep(2);
                    std::cout << "子进程退出临界区" << std::endl;

                    if (!sem.signal(0)) {
                        perror("child sem_signal failed");
                    }
                } else {
                    perror("child sem_wait failed");
                }

                exit(0);

            } else {
                // === 父进程 ===
                sleep(1);  // 让子进程先获取信号量

                std::cout << "父进程尝试获取信号量..." << std::endl;

                if (sem.wait(0)) {
                    std::cout << "父进程获得信号量，进入临界区" << std::endl;
                    std::cout << "父进程在临界区工作..." << std::endl;
                    sleep(1);
                    std::cout << "父进程退出临界区" << std::endl;

                    if (!sem.signal(0)) {
                        perror("parent sem_signal failed");
                    }
                } else {
                    perror("parent sem_wait failed");
                }

                wait(nullptr);

                // 清理信号量
                if (!sem.remove()) {
                    perror("Failed to remove semaphore");
                }
            }

        } catch (const std::exception& e) {
            std::cerr << "异常: " << e.what() << std::endl;
        }

        std::cout << "基础信号量示例完成" << std::endl;
    }

    // 示例2：生产者-消费者问题（使用System V信号量）
    void producerConsumerExample() {
        std::cout << "\n=== 示例2：生产者-消费者问题 ===" << std::endl;

        const int BUFFER_SIZE = 5;

        struct Buffer {
            int items[BUFFER_SIZE];
            int in;   // 生产者索引
            int out;  // 消费者索引
            int count;
        };

        try {
            // 创建信号量集合：0=互斥锁, 1=空槽位, 2=已用槽位
            SystemVSemaphore sem(IPC_PRIVATE, 3, IPC_CREAT | 0666);

            // 初始化信号量
            sem.setValue(0, 1);            // mutex = 1
            sem.setValue(1, BUFFER_SIZE);  // empty = BUFFER_SIZE
            sem.setValue(2, 0);            // full = 0

            // 创建共享内存
            int shmid = shmget(IPC_PRIVATE, sizeof(Buffer), IPC_CREAT | 0666);
            if (shmid == -1) {
                perror("shmget failed");
                return;
            }

            Buffer* buffer = (Buffer*)shmat(shmid, nullptr, 0);
            if (buffer == (Buffer*)-1) {
                perror("shmat failed");
                return;
            }

            // 初始化缓冲区
            buffer->in = 0;
            buffer->out = 0;
            buffer->count = 0;

            std::cout << "缓冲区大小: " << BUFFER_SIZE << std::endl;

            pid_t producer_pid = fork();
            if (producer_pid == 0) {
                // === 生产者进程 ===
                std::cout << "生产者进程启动" << std::endl;

                for (int i = 1; i <= 10; i++) {
                    // wait(empty)
                    sem.wait(1);
                    // wait(mutex)
                    sem.wait(0);

                    // 临界区：生产项目
                    buffer->items[buffer->in] = i;
                    std::cout << "生产者生产项目 " << i << " 到位置 "
                              << buffer->in
                              << " (缓冲区: " << (buffer->count + 1) << "/"
                              << BUFFER_SIZE << ")" << std::endl;
                    buffer->in = (buffer->in + 1) % BUFFER_SIZE;
                    buffer->count++;

                    // signal(mutex)
                    sem.signal(0);
                    // signal(full)
                    sem.signal(2);

                    usleep(200000);  // 生产延迟
                }

                std::cout << "生产者完成" << std::endl;
                exit(0);
            }

            pid_t consumer_pid = fork();
            if (consumer_pid == 0) {
                // === 消费者进程 ===
                std::cout << "消费者进程启动" << std::endl;

                for (int i = 0; i < 10; i++) {
                    // wait(full)
                    sem.wait(2);
                    // wait(mutex)
                    sem.wait(0);

                    // 临界区：消费项目
                    int item = buffer->items[buffer->out];
                    std::cout << "消费者消费项目 " << item << " 从位置 "
                              << buffer->out
                              << " (缓冲区: " << (buffer->count - 1) << "/"
                              << BUFFER_SIZE << ")" << std::endl;
                    buffer->out = (buffer->out + 1) % BUFFER_SIZE;
                    buffer->count--;

                    // signal(mutex)
                    sem.signal(0);
                    // signal(empty)
                    sem.signal(1);

                    usleep(300000);  // 消费延迟
                }

                std::cout << "消费者完成" << std::endl;
                exit(0);
            }

            // === 父进程 ===
            // 等待子进程完成
            wait(nullptr);
            wait(nullptr);

            // 清理资源
            shmdt(buffer);
            shmctl(shmid, IPC_RMID, nullptr);
            sem.remove();

        } catch (const std::exception& e) {
            std::cerr << "异常: " << e.what() << std::endl;
        }

        std::cout << "生产者-消费者示例完成" << std::endl;
    }

    // 示例3：读者-写者问题
    void readerWriterExample() {
        std::cout << "\n=== 示例3：读者-写者问题 ===" << std::endl;

        try {
            // 信号量：0=读者互斥, 1=写者互斥
            SystemVSemaphore sem(IPC_PRIVATE, 2, IPC_CREAT | 0666);
            sem.setValue(0, 1);  // reader_mutex = 1
            sem.setValue(1, 1);  // writer_mutex = 1

            // 共享内存
            int shmid =
                shmget(IPC_PRIVATE, sizeof(SharedResource), IPC_CREAT | 0666);
            SharedResource* resource =
                (SharedResource*)shmat(shmid, nullptr, 0);

            // 初始化
            resource->counter = 0;
            strcpy(resource->message, "初始数据");
            resource->reader_count = 0;
            resource->writer_active = false;

            // 创建写者进程
            pid_t writer_pid = fork();
            if (writer_pid == 0) {
                // === 写者进程 ===
                std::cout << "写者进程启动" << std::endl;

                for (int i = 1; i <= 3; i++) {
                    sem.wait(1);  // wait(writer_mutex)

                    resource->writer_active = true;
                    std::cout << "写者开始写入数据 " << i << std::endl;

                    resource->counter = i * 100;
                    snprintf(resource->message, sizeof(resource->message),
                             "写者数据-%d", i);

                    sleep(1);  // 模拟写入时间

                    std::cout << "写者完成写入: counter=" << resource->counter
                              << ", message=" << resource->message << std::endl;
                    resource->writer_active = false;

                    sem.signal(1);  // signal(writer_mutex)

                    sleep(2);  // 写者间隔
                }

                std::cout << "写者进程完成" << std::endl;
                exit(0);
            }

            // 创建多个读者进程
            for (int reader_id = 1; reader_id <= 3; reader_id++) {
                pid_t reader_pid = fork();
                if (reader_pid == 0) {
                    // === 读者进程 ===
                    std::cout << "读者-" << reader_id << " 进程启动"
                              << std::endl;

                    for (int i = 0; i < 4; i++) {
                        sem.wait(0);  // wait(reader_mutex)

                        resource->reader_count++;
                        if (resource->reader_count == 1) {
                            sem.wait(1);  // 第一个读者等待写者
                        }

                        sem.signal(0);  // signal(reader_mutex)

                        // 读取数据
                        std::cout << "读者-" << reader_id
                                  << " 读取: counter=" << resource->counter
                                  << ", message=" << resource->message
                                  << " (当前读者数: " << resource->reader_count
                                  << ")" << std::endl;

                        usleep(500000);  // 模拟读取时间

                        sem.wait(0);  // wait(reader_mutex)

                        resource->reader_count--;
                        if (resource->reader_count == 0) {
                            sem.signal(1);  // 最后一个读者释放写者
                        }

                        sem.signal(0);  // signal(reader_mutex)

                        sleep(1);  // 读者间隔
                    }

                    std::cout << "读者-" << reader_id << " 进程完成"
                              << std::endl;
                    exit(0);
                }
            }

            // === 父进程 ===
            // 等待所有子进程
            for (int i = 0; i < 4; i++) {
                wait(nullptr);
            }

            // 清理资源
            shmdt(resource);
            shmctl(shmid, IPC_RMID, nullptr);
            sem.remove();

        } catch (const std::exception& e) {
            std::cerr << "异常: " << e.what() << std::endl;
        }

        std::cout << "读者-写者示例完成" << std::endl;
    }

    // 示例4：POSIX命名信号量
    void posixNamedSemaphoreExample() {
        std::cout << "\n=== 示例4：POSIX命名信号量 ===" << std::endl;

        const char* sem_name = "/my_semaphore";

        // 清理可能存在的信号量
        sem_unlink(sem_name);

        // 创建命名信号量
        sem_t* semaphore = sem_open(sem_name, O_CREAT | O_EXCL, 0666, 1);
        if (semaphore == SEM_FAILED) {
            perror("sem_open failed");
            return;
        }

        std::cout << "创建POSIX命名信号量: " << sem_name << std::endl;

        pid_t pid = fork();
        if (pid == 0) {
            // === 子进程 ===
            // 重新打开信号量
            sem_t* child_sem = sem_open(sem_name, 0);
            if (child_sem == SEM_FAILED) {
                perror("child sem_open failed");
                exit(1);
            }

            std::cout << "子进程等待信号量..." << std::endl;

            if (sem_wait(child_sem) == 0) {
                std::cout << "子进程获得信号量，执行任务" << std::endl;
                sleep(2);
                std::cout << "子进程完成任务，释放信号量" << std::endl;
                sem_post(child_sem);
            } else {
                perror("child sem_wait failed");
            }

            sem_close(child_sem);
            exit(0);

        } else {
            // === 父进程 ===
            sleep(1);

            std::cout << "父进程等待信号量..." << std::endl;

            if (sem_wait(semaphore) == 0) {
                std::cout << "父进程获得信号量，执行任务" << std::endl;
                sleep(1);
                std::cout << "父进程完成任务，释放信号量" << std::endl;
                sem_post(semaphore);
            } else {
                perror("parent sem_wait failed");
            }

            wait(nullptr);
        }

        // 清理信号量
        sem_close(semaphore);
        sem_unlink(sem_name);

        std::cout << "POSIX命名信号量示例完成" << std::endl;
    }

    // 示例5：POSIX匿名信号量（进程间共享）
    void posixAnonymousSemaphoreExample() {
        std::cout << "\n=== 示例5：POSIX匿名信号量 ===" << std::endl;

        // 创建共享内存来存放信号量
        size_t size = sizeof(sem_t) + sizeof(int);
        void* shared_mem = mmap(nullptr, size, PROT_READ | PROT_WRITE,
                                MAP_SHARED | MAP_ANONYMOUS, -1, 0);
        if (shared_mem == MAP_FAILED) {
            perror("mmap failed");
            return;
        }

        sem_t* semaphore = (sem_t*)shared_mem;
        int* shared_counter = (int*)((char*)shared_mem + sizeof(sem_t));

        // 初始化匿名信号量（pshared=1表示进程间共享）
        if (sem_init(semaphore, 1, 1) == -1) {
            perror("sem_init failed");
            munmap(shared_mem, size);
            return;
        }

        *shared_counter = 0;

        std::cout << "创建POSIX匿名信号量" << std::endl;

        // 创建多个子进程
        for (int i = 1; i <= 3; i++) {
            pid_t pid = fork();
            if (pid == 0) {
                // === 子进程 ===
                std::cout << "进程-" << i << " 启动" << std::endl;

                for (int j = 0; j < 3; j++) {
                    if (sem_wait(semaphore) == 0) {
                        // 临界区
                        int old_value = *shared_counter;
                        std::cout << "进程-" << i
                                  << " 读取计数器: " << old_value << std::endl;

                        usleep(100000);  // 模拟工作

                        *shared_counter = old_value + 1;
                        std::cout << "进程-" << i
                                  << " 更新计数器: " << *shared_counter
                                  << std::endl;

                        sem_post(semaphore);
                    } else {
                        perror("sem_wait failed");
                    }

                    usleep(200000);
                }

                std::cout << "进程-" << i << " 完成" << std::endl;
                exit(0);
            }
        }

        // === 父进程 ===
        // 等待所有子进程
        for (int i = 0; i < 3; i++) {
            wait(nullptr);
        }

        std::cout << "最终计数器值: " << *shared_counter << std::endl;

        // 清理资源
        sem_destroy(semaphore);
        munmap(shared_mem, size);

        std::cout << "POSIX匿名信号量示例完成" << std::endl;
    }

    // 示例6：信号量的性能比较和最佳实践
    void performanceAndBestPractices() {
        std::cout << "\n=== 示例6：性能比较和最佳实践 ===" << std::endl;

        const int ITERATIONS = 1000;

        // 测试System V信号量性能
        auto start = std::chrono::high_resolution_clock::now();

        try {
            SystemVSemaphore sysv_sem(IPC_PRIVATE, 1, IPC_CREAT | 0666);
            sysv_sem.setValue(0, 1);

            for (int i = 0; i < ITERATIONS; i++) {
                sysv_sem.wait(0);
                sysv_sem.signal(0);
            }

            sysv_sem.remove();

        } catch (const std::exception& e) {
            std::cerr << "System V测试失败: " << e.what() << std::endl;
        }

        auto sysv_duration = std::chrono::high_resolution_clock::now() - start;

        // 测试POSIX信号量性能
        start = std::chrono::high_resolution_clock::now();

        const char* sem_name = "/perf_test_sem";
        sem_unlink(sem_name);

        sem_t* posix_sem = sem_open(sem_name, O_CREAT | O_EXCL, 0666, 1);
        if (posix_sem != SEM_FAILED) {
            for (int i = 0; i < ITERATIONS; i++) {
                sem_wait(posix_sem);
                sem_post(posix_sem);
            }

            sem_close(posix_sem);
            sem_unlink(sem_name);
        }

        auto posix_duration = std::chrono::high_resolution_clock::now() - start;

        // 输出性能结果
        std::cout << "性能比较 (" << ITERATIONS << " 次操作):" << std::endl;
        std::cout << "System V 信号量: "
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         sysv_duration)
                         .count()
                  << " 微秒" << std::endl;
        std::cout << "POSIX 信号量: "
                  << std::chrono::duration_cast<std::chrono::microseconds>(
                         posix_duration)
                         .count()
                  << " 微秒" << std::endl;

        std::cout << "\n最佳实践建议:" << std::endl;
        std::cout << "1. 优先使用POSIX信号量，性能更好且接口更简洁"
                  << std::endl;
        std::cout << "2. 对于简单的互斥，考虑使用pthread_mutex_t" << std::endl;
        std::cout << "3. 避免长时间持有信号量，减少阻塞时间" << std::endl;
        std::cout << "4. 使用命名信号量时注意清理，避免资源泄漏" << std::endl;
        std::cout << "5. 在多核系统上，考虑使用无锁数据结构" << std::endl;
    }

    void task() {
        std::cout << "=== 信号量 (Semaphore) 教学示例 ===" << std::endl;

        systemVBasicExample();             // 基础System V信号量
        producerConsumerExample();         // 生产者-消费者问题
        readerWriterExample();             // 读者-写者问题
        posixNamedSemaphoreExample();      // POSIX命名信号量
        posixAnonymousSemaphoreExample();  // POSIX匿名信号量
        performanceAndBestPractices();     // 性能比较和最佳实践
    }

}  // namespace Semaphore

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "     进程间通信 - 信号量教学示例" << std::endl;
    std::cout << "========================================" << std::endl;

    Semaphore::task();

    std::cout << "\n重要提示：" << std::endl;
    std::cout << "1. 信号量用于同步，不用于数据传输" << std::endl;
    std::cout << "2. 避免死锁：按固定顺序获取多个信号量" << std::endl;
    std::cout << "3. 使用 'ipcs -s' 查看System V信号量" << std::endl;
    std::cout << "4. 使用 'ipcrm -s <semid>' 删除孤立的信号量" << std::endl;
    std::cout << "5. POSIX信号量通常性能更好" << std::endl;
    std::cout << "6. 注意信号量的初始值设置" << std::endl;

    return 0;
}
