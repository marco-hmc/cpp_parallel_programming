#include <algorithm>
#include <boost/asio.hpp>
#include <boost/bind/bind.hpp>
#include <boost/coroutine2/all.hpp>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <numeric>
#include <queue>
#include <random>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

/*
====================================================================================================
                                Boost 协程 (C++17) 教学文档
====================================================================================================

1. 什么是Boost协程？
   - Boost.Coroutine2 是基于 C++11/14/17 的协程库
   - 提供了对称和非对称协程支持
   - 比C++20协程更底层，但更灵活
   - 支持栈式协程，可以在任意点暂停和恢复

2. 核心概念：
   - Coroutine: 协程对象，可以暂停和恢复执行
   - Push/Pull: 数据传递机制
   - Yield: 暂停执行并可选地传递数据
   - Resume: 恢复执行

3. 主要优势：
   - 避免回调地狱
   - 简化异步编程
   - 高效的内存使用
   - 更直观的控制流

4. 应用场景：
   - 生成器模式
   - 异步I/O操作
   - 状态机实现
   - 流水线处理

====================================================================================================
*/

using namespace std::chrono_literals;
using namespace boost::
    placeholders;  // 适配 <boost/bind/bind.hpp> 的占位符命名空间

// ================================================================================================
// 1. 基础协程概念 - 生成器模式
// ================================================================================================

namespace BasicCoroutines {

    // 类型别名，简化代码
    using coro_t = boost::coroutines2::coroutine<int>;

    // 简单的数字生成器
    void fibonacci_generator(coro_t::push_type& sink) {
        int a = 0, b = 1;
        while (true) {
            sink(a);  // yield value
            auto next = a + b;
            a = b;
            b = next;
        }
    }

    // 范围生成器
    void range_generator(coro_t::push_type& sink, int start, int end,
                         int step = 1) {
        for (int i = start; i < end; i += step) {
            sink(i);
        }
    }

    // 自定义生成器类
    class NumberGenerator {
      private:
        coro_t::pull_type source_;

      public:
        NumberGenerator(std::function<void(coro_t::push_type&)> generator)
            : source_(generator) {}

        class iterator {
          private:
            coro_t::pull_type* source_;

          public:
            iterator(coro_t::pull_type* src = nullptr) : source_(src) {}

            iterator& operator++() {
                if (source_) {
                    (*source_)();
                    if (!*source_) {
                        source_ = nullptr;
                    }
                }
                return *this;
            }

            int operator*() const { return source_->get(); }

            bool operator!=(const iterator& other) const {
                return source_ != other.source_;
            }
        };

        iterator begin() {
            if (source_) {
                return {&source_};
            }
            return end();
        }

        static iterator end() { return {}; }
    };

    void demonstrateBasicCoroutines() {
        std::cout << "\n=== 基础协程演示 ===" << std::endl;

        // 斐波那契数列生成器
        std::cout << "斐波那契数列 (前10个): ";
        coro_t::pull_type fibonacci_seq(fibonacci_generator);

        for (int i = 0; i < 10 && fibonacci_seq; fibonacci_seq(), ++i) {
            std::cout << fibonacci_seq.get() << " ";
        }
        std::cout << std::endl;

        // 范围生成器
        std::cout << "范围生成器 (1-10, 步长2): ";
        coro_t::pull_type range_seq(
            [](coro_t::push_type& sink) { range_generator(sink, 1, 10, 2); });

        // 使用自定义 NumberGenerator 类
        for (auto value : NumberGenerator([](coro_t::push_type& sink) {
                 range_generator(sink, 1, 10, 2);
             })) {
            std::cout << value << " ";
        }
        std::cout << std::endl;
    }

}  // namespace BasicCoroutines

// ================================================================================================
// 2. 字符串协程 - 文本处理
// ================================================================================================

namespace StringCoroutines {

    using string_coro_t = boost::coroutines2::coroutine<std::string>;

    // 单词分割生成器
    void word_splitter(string_coro_t::push_type& sink,
                       const std::string& text) {
        std::istringstream iss(text);
        std::string word;

        while (iss >> word) {
            sink(word);
        }
    }

    // 行读取生成器
    void line_reader(string_coro_t::push_type& sink,
                     const std::string& filename) {
        std::ifstream file(filename);
        std::string line;

        if (file.is_open()) {
            while (std::getline(file, line)) {
                sink(line);
            }
        } else {
            // 模拟文件内容
            std::vector<std::string> fake_lines = {"第一行内容", "第二行内容",
                                                   "第三行内容", "最后一行"};

            for (const auto& fake_line : fake_lines) {
                sink(fake_line);
            }
        }
    }

    // 数据变换协程
    void text_transformer(
        string_coro_t::push_type& sink, string_coro_t::pull_type& source,
        std::function<std::string(const std::string&)> transform) {
        while (source) {
            auto transformed = transform(source.get());
            sink(transformed);
            source();
        }
    }

    void demonstrateStringCoroutines() {
        std::cout << "\n=== 字符串协程演示 ===" << std::endl;

        // 单词分割
        std::string text = "Hello World Boost Coroutines Are Awesome";
        std::cout << "原文本: " << text << std::endl;
        std::cout << "分割单词: ";

        string_coro_t::pull_type word_seq(
            [&text](string_coro_t::push_type& sink) {
                word_splitter(sink, text);
            });

        while (word_seq) {
            std::cout << "[" << word_seq.get() << "] ";
            word_seq();
        }
        std::cout << std::endl;

        // 行读取和变换
        std::cout << "\n行读取和变换演示:" << std::endl;
        string_coro_t::pull_type line_seq([](string_coro_t::push_type& sink) {
            line_reader(sink, "nonexistent_file.txt");  // 使用模拟数据
        });

        while (line_seq) {
            std::string line = line_seq.get();
            std::cout << "原始: " << line << std::endl;
            std::cout << "大写: ";
            std::transform(line.begin(), line.end(), line.begin(), ::toupper);
            std::cout << line << std::endl;
            line_seq();
        }
    }

}  // namespace StringCoroutines

// ================================================================================================
// 3. 生产者-消费者模式
// ================================================================================================

namespace ProducerConsumer {

    using data_coro_t = boost::coroutines2::coroutine<int>;

    // 数据生产者
    class DataProducer {
      private:
        std::mt19937 gen_;
        std::uniform_int_distribution<> dis_;

      public:
        DataProducer(int min_val = 1, int max_val = 100)
            : gen_(std::random_device{}()), dis_(min_val, max_val) {}

        void produce_data(data_coro_t::push_type& sink, int count) {
            std::cout << "[Producer] 开始生产 " << count << " 个数据项"
                      << std::endl;

            for (int i = 0; i < count; ++i) {
                int value = dis_(gen_);
                std::cout << "[Producer] 生产数据: " << value << std::endl;

                // 模拟生产时间
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                sink(value);
            }

            std::cout << "[Producer] 生产完成" << std::endl;
        }
    };

    // 数据消费者
    class DataConsumer {
      private:
        std::vector<int> processed_data_;

      public:
        void consume_data(data_coro_t::pull_type& source) {
            std::cout << "[Consumer] 开始消费数据" << std::endl;

            while (source) {
                int value = source.get();

                // 模拟处理时间
                std::this_thread::sleep_for(std::chrono::milliseconds(50));

                // 简单的数据处理（平方）
                int processed = value * value;
                processed_data_.push_back(processed);

                std::cout << "[Consumer] 处理数据: " << value << " -> "
                          << processed << std::endl;

                source();
            }

            std::cout << "[Consumer] 消费完成" << std::endl;
        }

        const std::vector<int>& get_processed_data() const {
            return processed_data_;
        }

        void print_statistics() const {
            if (processed_data_.empty()) {
                std::cout << "没有处理任何数据" << std::endl;
                return;
            }

            auto sum = std::accumulate(processed_data_.begin(),
                                       processed_data_.end(), 0);
            auto avg = static_cast<double>(sum) / processed_data_.size();
            auto [min_it, max_it] = std::minmax_element(processed_data_.begin(),
                                                        processed_data_.end());

            std::cout << "处理统计:" << std::endl;
            std::cout << "  总数: " << processed_data_.size() << std::endl;
            std::cout << "  总和: " << sum << std::endl;
            std::cout << "  平均: " << avg << std::endl;
            std::cout << "  最小: " << *min_it << std::endl;
            std::cout << "  最大: " << *max_it << std::endl;
        }
    };

    void demonstrateProducerConsumer() {
        std::cout << "\n=== 生产者-消费者模式演示 ===" << std::endl;

        DataProducer producer;
        DataConsumer consumer;

        // 创建生产者协程
        data_coro_t::pull_type data_stream(
            [&producer](data_coro_t::push_type& sink) {
                producer.produce_data(sink, 5);
            });

        // 消费数据
        consumer.consume_data(data_stream);

        // 显示统计信息
        consumer.print_statistics();
    }

}  // namespace ProducerConsumer

// ================================================================================================
// 4. 异步I/O与协程 (使用Boost.Asio)
// ================================================================================================

namespace AsyncCoroutines {

    // 异步操作的协程包装
    template <typename T>
    class AsyncResult {
      private:
        using coro_t = boost::coroutines2::coroutine<T>;
        using push_t = typename coro_t::push_type;
        push_t* sink_;
        bool completed_;
        T result_;

      public:
        AsyncResult() : sink_(nullptr), completed_(false) {}

        void set_sink(push_t* sink) { sink_ = sink; }

        void complete(T result) {
            result_ = std::move(result);
            completed_ = true;
            if (sink_) {
                (*sink_)(result_);
            }
        }

        bool is_completed() const { return completed_; }
        const T& get_result() const { return result_; }
    };

    // 模拟异步文件读取
    class AsyncFileReader {
      public:
        using result_coro_t = boost::coroutines2::coroutine<std::string>;

        static void async_read_file(result_coro_t::push_type& sink,
                                    const std::string& filename) {
            std::cout << "[AsyncFileReader] 开始异步读取文件: " << filename
                      << std::endl;

            // 模拟异步操作
            std::thread([&sink, filename]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(200));

                // 模拟文件内容
                std::string content = "文件 " + filename + " 的内容\n";
                content += "这是第二行\n";
                content += "这是第三行\n";

                std::cout << "[AsyncFileReader] 文件读取完成: " << filename
                          << std::endl;
                sink(content);
            }).detach();
        }
    };

    // 模拟异步网络请求
    class AsyncHttpClient {
      public:
        struct HttpResponse {
            int status_code;
            std::string body;
            std::chrono::milliseconds response_time;
        };

        using response_coro_t = boost::coroutines2::coroutine<HttpResponse>;

        static void async_get(response_coro_t::push_type& sink,
                              const std::string& url) {
            std::cout << "[AsyncHttpClient] 发起HTTP请求: " << url << std::endl;

            std::thread([&sink, url]() {
                // 模拟网络延迟
                auto delay = std::chrono::milliseconds(300 + std::rand() % 200);
                std::this_thread::sleep_for(delay);

                // 模拟响应
                HttpResponse response;
                response.status_code =
                    (std::rand() % 10 < 8) ? 200 : 500;  // 80% 成功率
                response.body = "响应来自: " + url;
                response.response_time = delay;

                std::cout << "[AsyncHttpClient] HTTP请求完成: " << url
                          << " (状态: " << response.status_code << ")"
                          << std::endl;

                sink(response);
            }).detach();
        }
    };

    // 协程管道：组合多个异步操作
    void async_pipeline_demo() {
        std::cout << "\n=== 异步协程管道演示 ===" << std::endl;

        // 文件读取协程
        AsyncFileReader::result_coro_t::pull_type file_reader(
            [](AsyncFileReader::result_coro_t::push_type& sink) {
                AsyncFileReader::async_read_file(sink, "example.txt");
            });

        // HTTP请求协程
        std::vector<std::string> urls = {"https://api.example.com/data1",
                                         "https://api.example.com/data2",
                                         "https://api.example.com/data3"};

        std::cout << "启动多个异步操作..." << std::endl;

        // 等待文件读取完成
        if (file_reader) {
            std::string file_content = file_reader.get();
            std::cout << "文件内容:\n" << file_content << std::endl;
        }

        // 处理HTTP请求
        for (const auto& url : urls) {
            AsyncHttpClient::response_coro_t::pull_type http_response(
                [&url](AsyncHttpClient::response_coro_t::push_type& sink) {
                    AsyncHttpClient::async_get(sink, url);
                });

            if (http_response) {
                auto response = http_response.get();
                std::cout << "HTTP响应: " << response.body
                          << " (耗时: " << response.response_time.count()
                          << "ms)" << std::endl;
            }
        }
    }

    void demonstrateAsyncCoroutines() { async_pipeline_demo(); }
}  // namespace AsyncCoroutines
// ================================================================================================
// 5. 状态机协程
// ================================================================================================

namespace StateMachineCoroutines {

    // 状态枚举
    enum class State {
        Idle,
        Loading,
        Processing,
        Validating,
        Completed,
        Error
    };

    // 状态信息
    struct StateInfo {
        State state;
        std::string message;
        int progress;  // 0-100
    };

    using state_coro_t = boost::coroutines2::coroutine<StateInfo>;

    // 数据处理状态机
    class DataProcessingStateMachine {
      private:
        std::mt19937 gen_;
        std::uniform_int_distribution<> success_dis_;

        void emit_state(state_coro_t::push_type& sink, State state,
                        const std::string& message, int progress = 0) {
            StateInfo info{state, message, progress};
            std::cout << "[StateMachine] " << state_to_string(state) << ": "
                      << message;
            if (progress > 0) {
                std::cout << " (" << progress << "%)";
            }
            std::cout << std::endl;

            sink(info);
        }

        std::string state_to_string(State state) {
            switch (state) {
                case State::Idle:
                    return "空闲";
                case State::Loading:
                    return "加载中";
                case State::Processing:
                    return "处理中";
                case State::Validating:
                    return "验证中";
                case State::Completed:
                    return "完成";
                case State::Error:
                    return "错误";
                default:
                    return "未知";
            }
        }

      public:
        DataProcessingStateMachine()
            : gen_(std::random_device{}()), success_dis_(1, 10) {}

        void run_state_machine(state_coro_t::push_type& sink,
                               const std::string& data_id) {
            emit_state(sink, State::Idle, "等待处理数据: " + data_id);
            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            // 加载阶段
            emit_state(sink, State::Loading, "开始加载数据");
            for (int i = 10; i <= 100; i += 10) {
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
                emit_state(sink, State::Loading, "加载数据中", i);
            }

            // 检查加载是否成功
            if (success_dis_(gen_) <= 2) {  // 20% 失败率
                emit_state(sink, State::Error, "数据加载失败");
                return;
            }

            // 处理阶段
            emit_state(sink, State::Processing, "开始处理数据");
            for (int i = 20; i <= 100; i += 20) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                emit_state(sink, State::Processing, "处理数据中", i);
            }

            // 检查处理是否成功
            if (success_dis_(gen_) <= 1) {  // 10% 失败率
                emit_state(sink, State::Error, "数据处理失败");
                return;
            }

            // 验证阶段
            emit_state(sink, State::Validating, "开始验证结果");
            std::this_thread::sleep_for(std::chrono::milliseconds(200));

            // 检查验证是否成功
            if (success_dis_(gen_) <= 1) {  // 10% 失败率
                emit_state(sink, State::Error, "数据验证失败");
                return;
            }

            emit_state(sink, State::Completed, "数据处理完成", 100);
        }
    };

    void demonstrateStateMachine() {
        std::cout << "\n=== 状态机协程演示 ===" << std::endl;

        DataProcessingStateMachine state_machine;

        for (int i = 1; i <= 3; ++i) {
            std::cout << "\n--- 处理数据集 " << i << " ---" << std::endl;

            state_coro_t::pull_type state_sequence(
                [&state_machine, i](state_coro_t::push_type& sink) {
                    state_machine.run_state_machine(
                        sink, "DataSet" + std::to_string(i));
                });

            State final_state = State::Idle;
            while (state_sequence) {
                StateInfo info = state_sequence.get();
                final_state = info.state;

                // 可以在这里添加状态变化的处理逻辑
                if (info.state == State::Error) {
                    std::cout << "错误处理: " << info.message << std::endl;
                    break;
                }

                state_sequence();
            }

            std::cout << "最终状态: "
                      << (final_state == State::Completed ? "成功" : "失败")
                      << std::endl;
        }
    }

}  // namespace StateMachineCoroutines

// ================================================================================================
// 6. 协程管道和过滤器
// ================================================================================================

namespace PipelineCoroutines {

    using int_coro_t = boost::coroutines2::coroutine<int>;

    // 数据源
    void data_source(int_coro_t::push_type& sink, int count) {
        std::cout << "[DataSource] 生成 " << count << " 个数据项" << std::endl;
        std::mt19937 gen(std::random_device{}());
        std::uniform_int_distribution<> dis(1, 100);

        for (int i = 0; i < count; ++i) {
            int value = dis(gen);
            std::cout << "[DataSource] -> " << value << std::endl;
            sink(value);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }

    // 过滤器：只通过偶数
    void even_filter(int_coro_t::push_type& sink,
                     int_coro_t::pull_type& source) {
        std::cout << "[EvenFilter] 启动偶数过滤器" << std::endl;

        while (source) {
            int value = source.get();
            if (value % 2 == 0) {
                std::cout << "[EvenFilter] " << value << " -> 通过"
                          << std::endl;
                sink(value);
            } else {
                std::cout << "[EvenFilter] " << value << " -> 过滤"
                          << std::endl;
            }
            source();
        }
    }

    // 变换器：平方
    void square_transformer(int_coro_t::push_type& sink,
                            int_coro_t::pull_type& source) {
        std::cout << "[SquareTransformer] 启动平方变换器" << std::endl;

        while (source) {
            int value = source.get();
            int squared = value * value;
            std::cout << "[SquareTransformer] " << value << " -> " << squared
                      << std::endl;
            sink(squared);
            source();
        }
    }

    // 数据汇集
    void data_sink(int_coro_t::pull_type& source) {
        std::cout << "[DataSink] 开始收集数据" << std::endl;
        std::vector<int> collected;

        while (source) {
            int value = source.get();
            collected.push_back(value);
            std::cout << "[DataSink] <- " << value << std::endl;
            source();
        }

        std::cout << "[DataSink] 收集完成，总计: " << collected.size() << " 项"
                  << std::endl;

        if (!collected.empty()) {
            auto sum = std::accumulate(collected.begin(), collected.end(), 0);
            auto avg = static_cast<double>(sum) / collected.size();
            std::cout << "[DataSink] 统计 - 总和: " << sum << ", 平均: " << avg
                      << std::endl;
        }
    }

    void demonstratePipeline() {
        std::cout << "\n=== 协程管道演示 ===" << std::endl;

        // 构建处理管道: 数据源 -> 偶数过滤 -> 平方变换 -> 数据汇集
        std::cout << "构建处理管道: 数据源 -> 偶数过滤 -> 平方变换 -> 数据汇集"
                  << std::endl;

        // 创建数据源
        int_coro_t::pull_type source_stream(
            [](int_coro_t::push_type& sink) { data_source(sink, 10); });

        // 添加偶数过滤器
        int_coro_t::pull_type filtered_stream(
            [&source_stream](int_coro_t::push_type& sink) {
                even_filter(sink, source_stream);
            });

        // 添加平方变换器
        int_coro_t::pull_type transformed_stream(
            [&filtered_stream](int_coro_t::push_type& sink) {
                square_transformer(sink, filtered_stream);
            });

        // 数据汇集
        data_sink(transformed_stream);
    }

}  // namespace PipelineCoroutines

// ================================================================================================
// 7. 错误处理和异常
// ================================================================================================

namespace ErrorHandling {

    using result_coro_t =
        boost::coroutines2::coroutine<std::pair<bool, std::string>>;

    // 可能失败的操作
    class RiskyOperation {
      private:
        std::mt19937 gen_;
        std::uniform_int_distribution<> success_dis_;

      public:
        RiskyOperation() : gen_(std::random_device{}()), success_dis_(1, 10) {}

        void execute_operation(result_coro_t::push_type& sink,
                               const std::string& operation_name, int steps) {
            std::cout << "[RiskyOperation] 开始执行: " << operation_name
                      << std::endl;

            for (int i = 1; i <= steps; ++i) {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));

                // 模拟随机失败
                if (success_dis_(gen_) <= 2) {  // 20% 失败率
                    std::string error_msg =
                        "步骤 " + std::to_string(i) + " 失败";
                    std::cout << "[RiskyOperation] 错误: " << error_msg
                              << std::endl;
                    sink({false, error_msg});
                    return;
                }

                std::string success_msg = "步骤 " + std::to_string(i) + " 完成";
                std::cout << "[RiskyOperation] " << success_msg << std::endl;
                sink({true, success_msg});
            }

            std::cout << "[RiskyOperation] 操作完成: " << operation_name
                      << std::endl;
        }
    };

    // 错误恢复协程
    void error_recovery_demo() {
        std::cout << "\n=== 错误处理协程演示 ===" << std::endl;

        RiskyOperation risky_op;

        for (int attempt = 1; attempt <= 3; ++attempt) {
            std::cout << "\n--- 尝试 " << attempt << " ---" << std::endl;

            bool operation_success = true;
            std::string last_error;

            result_coro_t::pull_type operation_result(
                [&risky_op, attempt](result_coro_t::push_type& sink) {
                    risky_op.execute_operation(
                        sink, "CriticalTask" + std::to_string(attempt), 5);
                });

            while (operation_result) {
                auto [success, message] = operation_result.get();

                if (!success) {
                    operation_success = false;
                    last_error = message;
                    std::cout << "操作失败: " << message << std::endl;
                    break;
                }

                operation_result();
            }

            if (operation_success) {
                std::cout << "操作成功完成!" << std::endl;
                break;
            } else {
                std::cout << "操作失败，准备重试..." << std::endl;
                if (attempt == 3) {
                    std::cout << "所有重试均失败，最后错误: " << last_error
                              << std::endl;
                }
            }
        }
    }

    void demonstrateErrorHandling() { error_recovery_demo(); }

}  // namespace ErrorHandling

// ================================================================================================
// 8. 性能测试和比较
// ================================================================================================

namespace PerformanceTesting {

    // 传统回调方式
    void traditional_callback_chain() {
        std::cout << "\n--- 传统回调方式 ---" << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();

        std::function<void(int)> step3 = [start_time](int result) {
            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);
            std::cout << "回调链完成: " << result
                      << " (耗时: " << duration.count() << "ms)" << std::endl;
        };

        std::function<void(int)> step2 = [step3](int result) {
            std::cout << "回调步骤2: " << result << std::endl;
            std::thread([step3, result]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                step3(result * 3);
            }).detach();
        };

        std::function<void(int)> step1 = [step2](int result) {
            std::cout << "回调步骤1: " << result << std::endl;
            std::thread([step2, result]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(100));
                step2(result * 2);
            }).detach();
        };

        std::thread([step1]() {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            step1(10);
        }).detach();

        std::this_thread::sleep_for(
            std::chrono::milliseconds(500));  // 等待完成
    }

    // 协程方式
    void coroutine_chain() {
        std::cout << "\n--- 协程方式 ---" << std::endl;
        auto start_time = std::chrono::high_resolution_clock::now();

        using int_coro_t = boost::coroutines2::coroutine<int>;

        int_coro_t::pull_type chain([start_time](int_coro_t::push_type& sink) {
            int result = 10;

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            result *= 2;
            std::cout << "协程步骤1: " << result << std::endl;
            sink(result);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            result *= 3;
            std::cout << "协程步骤2: " << result << std::endl;
            sink(result);

            std::this_thread::sleep_for(std::chrono::milliseconds(100));

            auto end_time = std::chrono::high_resolution_clock::now();
            auto duration =
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    end_time - start_time);

            std::cout << "协程链完成: " << result
                      << " (耗时: " << duration.count() << "ms)" << std::endl;
            sink(result);
        });

        while (chain) {
            int result = chain.get();
            chain();
        }
    }

    void demonstratePerformance() {
        std::cout << "\n=== 性能测试和比较 ===" << std::endl;

        traditional_callback_chain();
        coroutine_chain();

        std::cout << "\nBoost协程优势:" << std::endl;
        std::cout << "1. 更清晰的控制流" << std::endl;
        std::cout << "2. 避免回调地狱" << std::endl;
        std::cout << "3. 更容易的错误处理" << std::endl;
        std::cout << "4. 更好的可维护性" << std::endl;
        std::cout << "5. 高效的内存使用" << std::endl;
    }

}  // namespace PerformanceTesting

// ================================================================================================
// 主函数 - 运行所有演示
// ================================================================================================

int main() {
    std::cout << "========================================" << std::endl;
    std::cout << "     Boost 协程 (C++17) 教学示例" << std::endl;
    std::cout << "========================================" << std::endl;

    try {
        // 基础协程概念
        BasicCoroutines::demonstrateBasicCoroutines();

        // 字符串协程
        StringCoroutines::demonstrateStringCoroutines();

        // 生产者-消费者模式
        ProducerConsumer::demonstrateProducerConsumer();

        // 异步协程
        AsyncCoroutines::demonstrateAsyncCoroutines();

        // 状态机协程
        StateMachineCoroutines::demonstrateStateMachine();

        // 管道协程
        PipelineCoroutines::demonstratePipeline();

        // 错误处理
        ErrorHandling::demonstrateErrorHandling();

        // 性能测试
        PerformanceTesting::demonstratePerformance();

    } catch (const std::exception& e) {
        std::cout << "程序异常: " << e.what() << std::endl;
        return 1;
    }

    std::cout << "\n重要总结：" << std::endl;
    std::cout << "1. Boost协程提供了栈式协程支持" << std::endl;
    std::cout << "2. 支持推拉（push/pull）数据传递模式" << std::endl;
    std::cout << "3. 可以在任意点暂停和恢复执行" << std::endl;
    std::cout << "4. 适用于生成器、异步操作、状态机等场景" << std::endl;
    std::cout << "5. 比传统回调更易于理解和维护" << std::endl;
    std::cout << "6. 在C++17环境下提供协程能力" << std::endl;

    return 0;
}
