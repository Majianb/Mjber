#ifndef SCHEDULER
#define SCHEDULER
#include <iostream>
#include <vector>
#include <map>
#include <unordered_map>

#include "thread_pool.h"
#include "fiber.h"


// 线程池可以不断压入任务



/*  IO协程调度器
    每当进行io操作时先addEvent，然后阻塞自己，线程会空闲，由线程池继续分配任务
    接口：
        addTask() //加入任务
        addEvent() //注册事件并阻塞自己
    
*/


// 单例模式
class IOScheduler {
public:
    IOScheduler(size_t threadCount = 1):threadPool(threadCount){};

    virtual ~IOScheduler() = default;
    //注册事件，调度点
    virtual void addEvent(int fd, uint32_t events, std::shared_ptr<Fiber> fiber) = 0;
    //调用的协程会阻塞自己来让线程进行其他工作
    void wait(){
        Fiber::GetThis()->yield();
    }
    
    //添加任务
    template<typename F,typename... Args>
    void addTask(F&& f,Args&&... args);

    //
    


    //获取下一个需要执行的
    static std::shared_ptr<IOScheduler> getIOScheduler(size_t threadCount);
    static std::shared_ptr<IOScheduler> gloabalIOScheduler;
protected:
    std::mutex mutex_; //为了维护队列的访问
    std::queue<Fiber::ptr> ready_list;
    ThreadPool threadPool;
    
};

// std::shared_ptr<IOScheduler> IOScheduler::gloabalIOScheduler = std::make_shared<IOScheduler>(4);


//添加一个任务,线程池已经保证线程安全
template<typename F,typename... Args>
void IOScheduler::addTask(F&& f,Args&&... args){
    std::shared_ptr<Fiber> work_fiber = Fiber::Create(f,args...);
    auto rtask = [](std::shared_ptr<Fiber> fiber){
        std::cout<<"fiber thread"<<std::endl;
        fiber->start();
        std::cout<<"fiber out"<<std::endl;
    };
    threadPool.enqueue(rtask,work_fiber);
}

static std::shared_ptr<IOScheduler> globalScheduler = nullptr;



//实现不同平台的IO调度器
//总是需要一个线程来不断收集到来的事件并唤醒

#ifdef _WIN32
#include <ws2tcpip.h>
#include <windows.h>

// Windows 平台的 IO 协程调度器
// 使用IOCP
// CreateIoCompletionPort用于创建一个端口
class WinIOScheduler : public IOScheduler {
public:
    WinIOScheduler(size_t threadCount = 1) :IOScheduler(threadCount){
        iocp = CreateIoCompletionPort(INVALID_HANDLE_VALUE, NULL, 0, 0);
        if (iocp == NULL) {
            throw std::runtime_error("Failed to create IO Completion Port");
        }
        threadPool.enqueue([this] {
            std::cout<<"scheduler thread begin";
            this->run();
        });
    }

    ~WinIOScheduler() {
        CloseHandle(iocp);
    }

    void addEvent(int fd, uint32_t events, std::shared_ptr<Fiber> fiber) override {
        HANDLE handle = reinterpret_cast<HANDLE>(fd);
        if (CreateIoCompletionPort(handle, iocp, reinterpret_cast<ULONG_PTR>(fiber.get()), 0) == NULL) {
            throw std::runtime_error("Failed to associate handle with IO Completion Port");
        }
    }

private:
    // 调度流程
    void run() {
        DWORD bytesTransferred;
        ULONG_PTR completionKey;
        LPOVERLAPPED overlapped;
        while (true) {
            // 等待返回完成端口 INFINITE代表阻塞
            if (GetQueuedCompletionStatus(iocp, &bytesTransferred, &completionKey, &overlapped, INFINITE)) {
                Fiber* fiber = reinterpret_cast<Fiber*>(completionKey);
                if (fiber) {
                    auto rtask = [](Fiber* fiber){
                        fiber->resume();
                    };
                    threadPool.enqueue(rtask,fiber);
                }
            }
        }
    }

    HANDLE iocp;
};
#else

#include <sys/epoll.h>
#include <unistd.h>

// Linux 平台的 IO 协程调度器
class LinuxIOScheduler : public IOScheduler {
public:
    LinuxIOScheduler(size_t threadCount = 1) : epollFd(epoll_create1(0)), threadPool(threadCount) {
        if (epollFd == -1) {
            throw std::runtime_error("Failed to create epoll instance");
        }
        for (size_t i = 0; i < threadCount; ++i) {
            threadPool.enqueue([this] {
                this->run();
            });
        }
    }

    ~LinuxIOScheduler() {
        close(epollFd);
    }

    void addEvent(int fd, uint32_t events, std::shared_ptr<Fiber> fiber) override {
        epoll_event ev;
        ev.events = events;
        ev.data.ptr = fiber.get();
        if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
            throw std::runtime_error("Failed to add event to epoll");
        }
        {
            std::unique_lock<std::mutex> lock(mutex);
            fiberMap[fd] = fiber;
        }
    }

private:
    void run() {
        epoll_event events[10];
        while (true) {
            int nfds = epoll_wait(epollFd, events, 10, -1);
            if (nfds == -1) {
                std::cerr << "epoll_wait error" << std::endl;
                continue;
            }
            for (int i = 0; i < nfds; ++i) {
                Fiber* fiber = static_cast<Fiber*>(events[i].data.ptr);
                if (fiber) {
                    {
                        std::unique_lock<std::mutex> lock(mutex);
                        auto it = fiberMap.find(events[i].data.fd);
                        if (it != fiberMap.end()) {
                            fiberMap.erase(it);
                        }
                    }
                    fiber->start();
                }
            }
        }
    }

    int epollFd;
    ThreadPool threadPool;
    std::unordered_map<int, std::shared_ptr<Fiber>> fiberMap;
    std::mutex mutex;
};
#endif

#ifdef _WIN32
    #define FiberScheduler WinIOScheduler
#else
    #define FiberSceduler LinuxIOScheduler
#endif

#endif