#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/types.h>
#include <unistd.h>

#include <iostream>

/*

1. **`shmget()`的使用**：

   `shmget()`函数用于创建或访问一个共享内存段。它的原型如下：
   ```cpp
   int shmget(key_t key, size_t size, int shmflg);
   ```

    `key`：共享内存的键值。可以指定一个具体的值，或者使用`IPC_PRIVATE`创建一个新的共享内存段。
    - `size`：共享内存段的大小，以字节为单位。

    `shmflg`：权限标志，通常是权限位（如`0644`）与`IPC_CREAT`、`IPC_EXCL`等标志的组合。
    返回值是共享内存段的标识符（ID），用于后续的操作。

2. **`shmat()`的使用**：

   `shmat()`函数将共享内存段连接到进程的地址空间中。它的原型如下：
   ```cpp
   void *shmat(int shmid, const void *shmaddr, int shmflg);
   ```
   - `shmid`：`shmget()`返回的共享内存标识符。
   -
    `shmaddr`：指定共享内存连接到进程地址空间中的具体地址。通常设置为`NULL`，让系统自动选择地址。
    - `shmflg`：操作标志，通常是`0`或`SHM_RDONLY`（只读连接）。
    返回值是指向共享内存段第一个字节的指针。

3. **`shmdt()`的使用**：
   `shmdt()`函数用于断开共享内存段与当前进程地址空间的连接。它的原型如下：
   ```cpp
   int shmdt(const void *shmaddr);
   ```
   - `shmaddr`：`shmat()`返回的地址指针。
   返回值为`0`表示成功，`-1`表示失败。

4.
**`shmctl()`的使用**：
   `shmctl()`函数用于对共享内存段执行各种控制操作。它的原型如下：
   ```cpp
   int shmctl(int shmid, int cmd, struct shmid_ds *buf);
   ```
   - `shmid`：共享内存标识符。

    `cmd`：控制命令，如`IPC_STAT`（获取共享内存的状态）、`IPC_SET`（设置共享内存的参数）、`IPC_RMID`（删除共享内存段）等。
    - `buf`：指向`shmid_ds`结构的指针，用于存储或设置共享内存的状态信息。
    返回值为`0`表示成功，`-1`表示失败。

5. **`IPC_PRIVATE`和`IPC_CREAT`的含义**：

    `IPC_PRIVATE`：创建一个新的共享内存段。即使键值相同，每次调用`shmget()`时也会创建一个新的共享内存段。
    -
    `IPC_CREAT`：与键值相对应的共享内存段不存在时，创建一个新的共享内存段。如果该共享内存段已存在，则返回现有的ID。通常与权限位一起使用，如`IPC_CREAT
    | 0666`。
*/

int main() {
    int shmid = shmget(IPC_PRIVATE, sizeof(int), IPC_CREAT | 0666);
    if (shmid == -1) {
        std::cerr << "Failed to create shared memory" << '\n';
        return 1;
    }

    int *sharedData = (int *)shmat(shmid, nullptr, 0);
    if (sharedData == (int *)-1) {
        std::cerr << "Failed to attach shared memory" << '\n';
        return 1;
    }

    pid_t pid = fork();
    if (pid == -1) {
        std::cerr << "Failed to create child process" << '\n';
        return 1;
    }

    if (pid == 0) {
        *sharedData = 42;
        std::cout << "Child process wrote data to shared memory" << '\n';
    } else {
        sleep(1);
        std::cout << "Parent process read data from shared memory: "
                  << *sharedData << '\n';

        if (shmdt(sharedData) == -1) {
            std::cerr << "Failed to detach shared memory" << '\n';
            return 1;
        }

        if (shmctl(shmid, IPC_RMID, nullptr) == -1) {
            std::cerr << "Failed to delete shared memory" << '\n';
            return 1;
        }
    }

    return 0;
}
