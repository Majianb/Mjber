#ifndef SCHEDULER
#define SCHEDULER
#include <iostream>
#include <vector>
#include <ctime>
#include <map>
#include <unordered_map>
#include <thread>
#include "thread_pool.h"
#include "fiber.h"
#include "logger.h"

// 线程池可以不断压入任务



/*  IO协程调度器
    每当进行io操作时先addEvent，然后阻塞自己，线程会空闲，由线程池继续分配任务
    接口：
        addTask() //加入任务
        addEvent() //注册事件并阻塞自己
    需要一个表维护fd和fiber的对应关系
    
*/


/*
    事件类型，关联的协程，时间
*/
struct IOEvent{
    std::shared_ptr<Fiber> fiber_;
    enum IOType{
        READ,WRITE
    } type_;
    std::time_t time_;
};


// 单例模式
class IOScheduler {
public:
    IOScheduler(size_t threadCount = 1):threadPool(threadCount){};

    virtual ~IOScheduler() = default;
    //注册事件，调度点
    virtual void addEvent(int fd, uint32_t events, std::shared_ptr<Fiber> fiber) = 0;
    //调用的协程会阻塞自己来让线程进行其他工作
    void wait(){
        LOG_STREAM<<"Fiber yield"<<INFOLOG;
        Fiber::GetThis()->yield();
        LOG_STREAM<<"Fiber resume"<<INFOLOG;
    }
    
    //添加任务
    template<typename F,typename... Args>
    void addTask(F&& f,Args&&... args);

    //
    


    //获取下一个需要执行的
    static std::shared_ptr<IOScheduler> getIOScheduler(size_t threadCount);
    static std::shared_ptr<IOScheduler> gloabalIOScheduler;
protected:
    std::mutex registryMutex; //为了维护队列的访问
    std::queue<Fiber::ptr> ready_list;
    ThreadPool threadPool;
    std::map<int,IOEvent> fdRegistry;
};

// std::shared_ptr<IOScheduler> IOScheduler::gloabalIOScheduler = std::make_shared<IOScheduler>(4);


//添加一个任务,线程池已经保证线程安全
template<typename F,typename... Args>
void IOScheduler::addTask(F&& f,Args&&... args){
    std::shared_ptr<Fiber> work_fiber = Fiber::Create(f,args...);
    auto rtask = [](std::shared_ptr<Fiber> fiber){
        LOG_STREAM<<"Fiber "<< std::to_string(fiber->getID())<<" start"<<DEBUGLOG;
        fiber->start();
        LOG_STREAM<<"Fiber "<< std::to_string(fiber->getID())<<" end"<<DEBUGLOG;
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
        //  启动一个调度线程
        worker = std::thread(&WinIOScheduler::run, this);
    }

    ~WinIOScheduler() {
        CloseHandle(iocp);
    }

    void addEvent(int fd, uint32_t events, std::shared_ptr<Fiber> fiber) override {
        

        std::lock_guard<std::mutex> lock(registryMutex);
        if (fdRegistry.find(fd) != fdRegistry.end()) {
            return;
        }
        else{
            HANDLE handle = reinterpret_cast<HANDLE>(fd);
            if (CreateIoCompletionPort(handle, iocp, reinterpret_cast<ULONG_PTR>(fiber.get()), 0) == NULL) {
                DWORD errorCode = GetLastError();
                std::string errorMsg = "Failed to associate handle with IO Completion Port. Error code: " + std::to_string(errorCode);
                throw std::runtime_error(errorMsg);
            }
            fdRegistry[fd] = fiber;
            LOG_STREAM<<"fiber "<<std::to_string(fiber->getID())<<" add event"<<DEBUGLOG;
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
                    LOG_STREAM<<"fiber "<<std::to_string(fiber->getID())<<"get event"<<std::to_string(bytesTransferred)<<DEBUGLOG;
                    fiber->setIORes(bytesTransferred);
                    auto rtask = [](Fiber* fiber){
                        fiber->resume();
                    };
                    threadPool.enqueue(rtask,fiber);
                }
            }
        }
    }

    std::thread worker;
    HANDLE iocp;
    std::unordered_map<int, std::shared_ptr<Fiber>> fdRegistry; // 注册表
    std::mutex registryMutex; // 保护注册表的互斥锁
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
        worker = std::thread(&LinuxIOScheduler::run, this);
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
    }

private:
    void run() {
        epoll_event events[10];
        while (true) {
            int nfds = epoll_wait(epollFd, events, 10, -1);
            if (nfds == -1) {
                LOG_STREAM<<"epoll_wait error"<<ERRORLOG;
                continue;
            }
            for (int i = 0; i < nfds; ++i) {
                Fiber* fiber = static_cast<Fiber*>(events[i].data.ptr);
                if (fiber) {
                    if (fiber) {
                        LOG_STREAM<<"fiber "<<std::to_string(fiber->getID())<<" get event"<<DEBUGLOG;
                        auto rtask = [](Fiber* fiber){
                            fiber->resume();
                        };
                        threadPool.enqueue(rtask,fiber);
                    }
                }
            }
        }
    }

    int epollFd;
    std::thread worker;
};
#endif

#ifdef _WIN32
    #define FiberScheduler WinIOScheduler
#else
    #define FiberSceduler LinuxIOScheduler
#endif

#endif