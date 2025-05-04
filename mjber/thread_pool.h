#pragma once
// 线程池
#include <vector>
#include <queue>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <stdexcept>

class ThreadPool {
public:
    ThreadPool(size_t threads);
    ~ThreadPool();
    void stopWork();

    // 可变参数模板
    // std::result_of<F(Args...)>::type 元函数，推断F(Args...)的返回值类型
    // 返回一个future对象，用于获取异步任务的结果
    template<class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::result_of<F(Args...)>::type>;


private:
    // 工作线程集合
    std::vector<std::thread> workers;
    // 任务队列
    std::queue<std::function<void()>> tasks;
    
    // 同步相关
    std::mutex queue_mutex;
    std::condition_variable condition;
    bool stop;
};

// 添加任务
template <typename F, typename... Args>
auto ThreadPool::enqueue(F&& f, Args&&... args) 
    -> std::future<typename std::result_of<F(Args...)>::type> {
    using return_type = typename std::result_of<F(Args...)>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    // 添加任务，要上锁
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        if (stop) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        tasks.emplace([task]() { (*task)(); });
    }
    condition.notify_one();
    return res;
}

// 线程池构造函数
// 每个线程都等待任务，需要一个互斥量
ThreadPool::ThreadPool(size_t threadCount) : stop(false) {
    for (size_t i = 0; i < threadCount; ++i) {
        workers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(queue_mutex);
                    condition.wait(lock, [this] { return stop ||!tasks.empty();});
                    if (stop && tasks.empty()) {
                        return;
                    }
                    task = std::move(tasks.front());
                    tasks.pop();
                }
                std::cout<<"thread begin"<<std::endl;
                task();
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    stopWork();
    for (auto& thread : workers) {
        if (thread.joinable()) {
            thread.join();
        }
    }
}



// 停止工作线程
void ThreadPool::stopWork() {
    {
        std::unique_lock<std::mutex> lock(queue_mutex);
        stop = true;
    }
    condition.notify_all();
}


// emplace_back 直接构造元素，少了一次拷贝构造
// 完美转发
// std::bind 用于将可调用对象和参数绑定到一起，返回一个可调用对象
// std::future 捕获结果

// 维护一个任务队列std::function<void()>，线程池中的每个线程都等待任务队列中的任务然后执行
// 对于（函数f,...参数）
//      std::packaged_task<return_type()> 用于包装函数f，返回一个std::future<return_type>对象
//      使用shared_ptr管理std::packaged_task对象
//      最后使用匿名函数包装*task来添加一个引用，防止原task被析构
//      返回future对象，用于获取结果


// 改进：concurrent_queue
//       Work-Stealing
//       TLS


