#ifndef SCHEDULER
#define SCHEDULER
#include <iostream>
#include <vector>
#include <ctime>
#include <map>
#include <unordered_map>
#include <thread>
#include <utility>


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
    协程的描述符
    等待的事件类型，协程指针，时间
*/
class FiberDes{
public:
    #ifdef _WIN32
    FiberDes(std::shared_ptr<Fiber> fiber):fiber_(fiber),type_(IOType::NONE),fd_(-1),io_res_(0){}
    #else
    FiberDes(std::shared_ptr<Fiber> fiber):fiber_(fiber),type_(IOType::NONE),fd_(-1){}
    #endif

    std::shared_ptr<Fiber> fiber_;
    //NONE表示没等待事件
    enum IOType{
        READ,WRITE,NONE
    } type_;
    std::time_t time_;
    //阻塞的io字段
    int fd_;
    //对于windows用来传递IO完成结果
    #ifdef _WIN32
    size_t io_res_;
    #endif
};


// 单例模式
class IOScheduler {
public:
    IOScheduler(size_t threadCount = 1):threadPool(threadCount){};

    virtual ~IOScheduler() = default;
    /*
        协程内调用的方法
    */
    //--注册事件，调度点
    virtual void addEvent(int fd, uint32_t events) = 0;
    //--销毁事件，表示不需要再维护
    virtual void rmEvent(int fd) = 0;
    //--主动让出，调用的协程会阻塞自己来让线程进行其他工作
    void wait(){
        Fiber::GetThis()->yield();
    }
    //--销毁退出
    void exit(){
        auto fid = Fiber::GetThis()->getID();
        {
        std::lock_guard<std::mutex> lock(registryMutex);
        // 状态表
        if(Registry.find(fid)!=Registry.end())
            Registry.erase(Registry.find(fid));
        // 压入空闲列表
        freeFibers.emplace_back(Fiber::GetThis());
        }
        LOG_STREAM<<"Fiber "<< std::to_string(fid)<<" end"<<DEBUGLOG;
    }
    /*
        协程的管理
    */
    //--添加任务
    template<typename F,typename... Args>
    void addTask(F&& f,Args&&... args);
    //--检查协程是否有效
    bool checkFiber(int64_t fid){
        std::lock_guard<std::mutex> lock(registryMutex);
        return Registry.find(fid)!=Registry.end();
    }
    bool checkFiber(){
        std::lock_guard<std::mutex> lock(registryMutex);
        auto fid = Fiber::GetThis()->getID();
        return Registry.find(fid)!=Registry.end();
    }


    //获取下一个需要执行的
    static std::shared_ptr<IOScheduler> getIOScheduler(size_t threadCount);
    static std::shared_ptr<IOScheduler> gloabalIOScheduler;

protected:
    ThreadPool threadPool;
    std::mutex registryMutex; //为了维护注册表的访问
    std::unordered_map<uint64_t,std::shared_ptr<FiberDes>> Registry; //记录fiber的注册表
    std::vector<std::shared_ptr<Fiber>> freeFibers; //空闲fiber列表
};

// std::shared_ptr<IOScheduler> IOScheduler::gloabalIOScheduler = std::make_shared<IOScheduler>(4);


//添加一个任务,线程池已经保证线程安全
template<typename F,typename... Args>
void IOScheduler::addTask(F&& f,Args&&... args){
    // 1. 创建fiber
    bool hit = false;
    std::shared_ptr<Fiber> work_fiber;
    {
        std::lock_guard<std::mutex> lock(registryMutex);
        if(!freeFibers.empty()){
            work_fiber = freeFibers.back();
            freeFibers.pop_back();
            hit = true;
        }
    }
    if(hit){
        work_fiber->reuse(f,args...);
    }
    if(!hit){
        work_fiber = Fiber::Create(f,args...);
    }
    
    auto rtask = [this](std::shared_ptr<Fiber> fiber){
        LOG_STREAM<<"Fiber "<< std::to_string(fiber->getID())<<" start"<<DEBUGLOG;
        fiber->start();
    };
    auto call_back_task = [this](){
        this->exit();
    };
    work_fiber->setCallBack(call_back_task);
    
    // 2. 维护记录
    {
        auto f_id = work_fiber->getID();
        std::lock_guard<std::mutex> lock(registryMutex);
        Registry[f_id] = std::make_shared<FiberDes>(work_fiber);
    }
    // 3. 推入线程池
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
    LinuxIOScheduler(size_t threadCount = 1):IOScheduler(threadCount),epollFd(epoll_create1(0)){
        if (epollFd == -1) {
            throw std::runtime_error("Failed to create epoll instance");
        }
        worker = std::thread(&LinuxIOScheduler::run, this);
    }

    ~LinuxIOScheduler() {
        close(epollFd);
    }

    // 使用一个表来防止重复注册
    void addEvent(int fd, uint32_t events) override {
        // 检查协程
        if(!checkFiber()){
            throw std::runtime_error("Fiber has been deleted when addEvent");
        }
        std::lock_guard<std::mutex> lock(registryMutex);
        auto f_id = Fiber::GetThis()->getID();
        auto term = Registry.find(f_id);
        auto fiber_des = term->second;
        auto fd_state = EpollRegitry.find(fd);
        // 查询是否注册过epoll
        if(fd_state != EpollRegitry.end() && (fd_state->second)&events != 0){ // 注册过就直接修改描述符
            if(events&EPOLLIN != 0) fiber_des->type_ = FiberDes::READ;
            if(events&EPOLLOUT != 0) fiber_des->type_ = FiberDes::WRITE;
            return; 
        }
        else{       // 否则进入注册流程
            // 1.注册到epoll
            epoll_event ev;
            ev.events = events;
            ev.data.ptr = reinterpret_cast<void*>(f_id);
            if (epoll_ctl(epollFd, EPOLL_CTL_ADD, fd, &ev) == -1) {
                LOG_STREAM<<"epoll add error "<<errno<<ERRORLOG;
                throw std::runtime_error("Failed to add event to epoll");
            }
            // 2.维护注册表
            if(fd_state == EpollRegitry.end()) EpollRegitry[fd] = events;
            else EpollRegitry[fd] = EpollRegitry[fd] | events;
            if(events&EPOLLIN != 0) fiber_des->type_ = FiberDes::READ;
            if(events&EPOLLOUT != 0) fiber_des->type_ = FiberDes::WRITE;
        }
    }
    // 销毁持有的套接字
    void rmEvent(int fd) override{
        std::lock_guard<std::mutex> lock(registryMutex);
        auto fd_state = EpollRegitry.find(fd);
        if(fd_state != EpollRegitry.end()){
            EpollRegitry.erase(fd); 
        }
        if (epoll_ctl(epollFd, EPOLL_CTL_DEL, fd,nullptr) == -1) {
            LOG_STREAM<<"epoll del error "<<errno<<ERRORLOG;
            throw std::runtime_error("Failed to add event to epoll");
        }
    }

private:
    void run() {
        epoll_event events[10];
        while (true) {
            int nfds = epoll_wait(epollFd, events, 10, -1);
            if (nfds == -1) {
                LOG_STREAM<<"epoll_wait error "<<errno<<ERRORLOG;
                continue;
            }
            else{
                for (int i = 0; i < nfds; ++i) {
                    uint64_t f_id = reinterpret_cast<uint64_t>(events[i].data.ptr);
                    auto fiber_events = events[i].events;
                    if(!checkFiber(f_id)){
                        throw std::runtime_error("Fiber has been deleted when resume");
                    }
                    auto term = Registry.find(f_id);
                    auto fiber_des = term->second;
                    // 确认是同类型的事件才唤醒
                    if(
                        (fiber_events&EPOLLIN != 0 && fiber_des->type_ == FiberDes::READ)
                        ||
                        (fiber_events&EPOLLOUT != 0 && fiber_des->type_ == FiberDes::WRITE)
                        ||
                        (fiber_events&EPOLLHUP || fiber_events&EPOLLERR)
                    ) {
                        fiber_des->type_ = FiberDes::NONE;
                        LOG_STREAM<<"fiber "<<std::to_string(f_id)<<" get event "<<std::to_string(fiber_events)<<DEBUGLOG;
                        auto rtask = [](Fiber* fiber){
                            fiber->resume();
                        };
                        threadPool.enqueue(rtask,fiber_des->fiber_.get());
                    }
                }
            }
        }
    }

    int epollFd;
    std::thread worker; 
    std::unordered_map<int,int> EpollRegitry; //避免epoll重复注册
};
#endif

#ifdef _WIN32
    #define FiberScheduler WinIOScheduler
#else
    #define FiberScheduler LinuxIOScheduler
#endif

#endif