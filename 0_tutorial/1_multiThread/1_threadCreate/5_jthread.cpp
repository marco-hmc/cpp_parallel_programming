#include <chrono>
#include <iostream>
#include <stop_token>
#include <thread>
#include <vector>

/*
  C++20 引入了 std::jthread 和 cooperative cancellation 机制，
  解决了 std::thread 的两大痛点：
  1. 必须在销毁前手动 join() 或 detach()，否则 std::terminate
  2. 没有标准化的线程取消机制
*/

// ============================================================
namespace jthread_basic {
    /*
    1. std::jthread 是什么？
        std::jthread（joining thread）是 std::thread 的升级版。
        在析构时自动 join()，不会导致 std::terminate。

    2. 基本用法：
        和 std::thread 用法几乎一样，只是不用手动 join()。

    3. 和 std::thread 的区别：
        - 析构时自动 join()
        - 自带 stop_source，支持 cooperative cancellation
        - 可移动但不可拷贝（和 std::thread 一样）
    */

    void worker(int id, int iterations) {
        for (int i = 0; i < iterations; ++i) {
            std::cout << "jthread " << id << " 迭代 " << i << "\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    void task() {
        std::cout << "使用 jthread（自动 join）:\n";
        {
            std::jthread t1(worker, 1, 3);
            std::jthread t2(worker, 2, 3);
            // 离开作用域时自动 join，不需要手动调用
        }
        std::cout << "jthread 已自动 join，主线程继续。\n";
    }
}  // namespace jthread_basic

// ============================================================
namespace stop_token_basic {
    /*
    1. std::stop_token / std::stop_source 是什么？
        - std::stop_source: 发出停止请求的一端
        - std::stop_token: 检查停止请求的一端
        - std::stop_callback: 注册停止时的回调

        jthread 自带一个 stop_source，通过 get_stop_source() 获取，
        线程函数可以通过 get_stop_token() 获取 stop_token 来检查。

    2. 为什么需要这个？
        传统的线程取消方式（如设置 atomic<bool> flag）有局限性：
        - 线程正在 sleep 时无法及时响应
        - 需要手动管理 flag 生命周期
        stop_token 可以与 condition_variable::wait 等配合，
        用 interruptible wait 实现即时响应。
    */

    void interruptible_worker(std::stop_token stoken, int id) {
        for (int i = 0; ; ++i) {
            // 检查是否收到停止请求
            if (stoken.stop_requested()) {
                std::cout << "jthread " << id << " 收到停止请求，退出（迭代 "
                          << i << " 次）\n";
                return;
            }

            std::cout << "jthread " << id << " 工作中... 迭代 " << i << "\n";

            // 使用 condition_variable_any 的 interruptible wait
            // 这里用简单的 poll 方式演示
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    }

    void task() {
        std::cout << "使用 stop_token 取消线程:\n";

        std::jthread t1(interruptible_worker, 1);
        std::jthread t2(interruptible_worker, 2);

        // 让线程运行一会儿
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        // 请求停止
        std::cout << "\n主线程: 发送停止请求...\n";
        t1.request_stop();
        t2.request_stop();

        // jthread 析构时会自动 join
        std::cout << "主线程: 等待线程退出...\n";
    }
}  // namespace stop_token_basic

// ============================================================
namespace stop_callback_example {
    /*
    1. std::stop_callback 是什么？
        注册一个回调函数，当 stop 被请求时自动调用。
        可以用于清理资源、日志记录等。

    2. 注意事项：
        - 回调在 request_stop() 的调用线程中执行
        - 回调的执行是同步的
    */

    void worker(std::stop_token stoken, int id) {
        // 注册停止回调
        std::stop_callback cb(stoken, [id] {
            std::cout << "[回调] 线程 " << id << " 正在清理资源...\n";
        });

        while (!stoken.stop_requested()) {
            std::cout << "线程 " << id << " 工作中...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        std::cout << "线程 " << id << " 退出。\n";
    }

    void task() {
        std::jthread t(worker, 1);

        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        std::cout << "主线程: 请求停止...\n";
        t.request_stop();
        // 回调会在这里（request_stop 的调用线程）执行
    }
}  // namespace stop_callback_example

// ============================================================
namespace stop_source_manual {
    /*
    1. 手动使用 stop_source：
        可以创建独立的 stop_source 和 stop_token，不必依赖 jthread。
        这在需要跨多个线程管理取消场景时非常有用。
    */

    void worker(std::stop_token stoken, int id) {
        while (!stoken.stop_requested()) {
            std::cout << "线程 " << id << " 工作中...\n";
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        std::cout << "线程 " << id << " 被取消。\n";
    }

    void task() {
        std::stop_source source;
        std::stop_token token = source.get_token();

        std::vector<std::jthread> threads;
        for (int i = 1; i <= 3; ++i) {
            threads.emplace_back(worker, token, i);
        }

        std::this_thread::sleep_for(std::chrono::milliseconds(300));
        std::cout << "\n主线程: 取消所有工作线程...\n";
        source.request_stop();
    }
}  // namespace stop_source_manual

// ============================================================
int main() {
    std::cout << "===== 1. jthread 基本用法（自动 join）=====\n";
    jthread_basic::task();

    std::cout << "\n===== 2. stop_token 取消线程 =====\n";
    stop_token_basic::task();

    std::cout << "\n===== 3. stop_callback 清理回调 =====\n";
    stop_callback_example::task();

    std::cout << "\n===== 4. 独立的 stop_source =====\n";
    stop_source_manual::task();

    return 0;
}
