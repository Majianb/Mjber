#pragma once
#include "context.h"

#include <functional>
#include <memory>
#include <atomic>
#include <utility>
#include <type_traits>
// 新建一个协程：使用构造函数传入函数
// 此时新建一个用于执行该函数的上下文
// start时开始执行任务协程，转到任务协程上下文继续执行
// 暂停时切换到主协程上下文，继续执行主协程
// 








// 协程状态枚举
enum class FiberState {
    INIT,   // 初始化
    HOLD,   // 挂起
    EXEC,   // 执行
    TERM,   // 终止
    READY   // 就绪
};
// start() init -> exec
// resume() ready -> exec
// yield() exec -> hold
// exec -> term
// 




class Fiber : public std::enable_shared_from_this<Fiber> {
public:
    using ptr = std::shared_ptr<Fiber>;
    using Func = std::function<void()>;

    // 工厂函数
    template <typename Fn, typename... Args>
    static ptr Create(Fn&& task, Args&&... args);
    //
    template <typename Fn, typename... Args>
    Fiber(Fn&& task, Args&&... args); //子协程
    Fiber(); // 主协程，也就是主要执行流
    // 析构函数
    ~Fiber();

    // 启动协程
    void start();
    // 恢复协程执行
    void resume();
    // 暂停协程执行
    void yield(Fiber::ptr nextfiber);

    // 获取当前协程状态
    FiberState getState() const { return m_state; }

    // 设置当前正在执行的协程
    static void SetThis(ptr co);
    static ptr GetThis();

private:
    // 协程入口函数,使用指针操作兼容C函数
    static void mainFunc(Fiber* fiber);

private:
    uint64_t m_id = 0;
    // static size_t m_stack_size;
    FiberState m_state = FiberState::INIT;
    Context m_ctx;
    char* m_stack = nullptr;
    Func m_task;

    static size_t m_stack_size;
    static thread_local std::shared_ptr<Fiber> mainFiber;
    static thread_local std::shared_ptr<Fiber> currentFiber;
};    

static std::atomic<uint64_t> s_Fiber_id {0};
size_t Fiber::m_stack_size = 1024*1024;
thread_local std::shared_ptr<Fiber> Fiber::currentFiber = nullptr;  // 代表当前正在执行的协程
thread_local std::shared_ptr<Fiber> Fiber::mainFiber = nullptr;


Fiber::Fiber(): m_id(++s_Fiber_id), m_stack(nullptr){
    m_state = FiberState::EXEC;
    m_ctx.firstIn = 0;
}


// 工作协程,接收工作函数和参数
template <typename Fn, typename... Args>
Fiber::Fiber(Fn&& intask, Args&&... args):m_id(++s_Fiber_id){
    
    m_stack = new char[m_stack_size]; //分配栈空间

    // 包装工作函数
    auto myfunc = std::bind(
        std::forward<Fn>(intask), std::forward<Args>(args)...
    );
    m_task = myfunc;
    // ctx_save(&m_ctx);

    m_ctx.funcPtr = reinterpret_cast<void*>(&Fiber::mainFunc);
    m_ctx.firstIn = (void*)1;
    m_ctx.ptr = this;

    m_ctx.rsp = m_stack + m_stack_size - sizeof(void*);
    m_state = FiberState::READY; //就绪

}

Fiber::~Fiber() {
    if(!m_stack) delete[] m_stack;
}
template <typename Fn, typename... Args>
Fiber::ptr Fiber::Create(Fn&& intask, Args&&... args){

    // 主协程懒加载
    if (Fiber::mainFiber==nullptr) {
        Fiber::mainFiber = std::make_shared<Fiber>();
        // // 主协程首次需要保存上下文
        // ctx_save(&(mainFiber->m_ctx));
    }
    auto temp = std::make_shared<Fiber>(std::forward<Fn>(intask), std::forward<Args>(args)...);
    return temp;
}


// 启动工作协程
void Fiber::start() { 
    m_state = FiberState::EXEC;
    SetThis(shared_from_this());
    ctx_swap(&(Fiber::mainFiber->m_ctx),&(m_ctx));
}

// 恢复工作协程执行
void Fiber::resume() {
    if (m_state != FiberState::HOLD) {
        return;
    }
    m_state = FiberState::EXEC;
    // 回到工作协程上下文
    SetThis(shared_from_this());
    ctx_swap(&(mainFiber->m_ctx),&(m_ctx));
}

// 暂停工作协程执行,回到下一个协程
// 如果没有则回到主协程
void Fiber::yield(Fiber::ptr nextfiber=nullptr) {
    if (m_state != FiberState::EXEC) {
        return;
    }
    m_state = FiberState::HOLD;
    if(nextfiber){
        ctx_swap(&m_ctx,&(nextfiber->m_ctx));
    }
    else{
        SetThis(mainFiber);
        ctx_swap(&m_ctx,&(Fiber::mainFiber->m_ctx));
    }
}

// 协程的工作函数
void Fiber::mainFunc(Fiber* fiber) {

    if (fiber->m_task) {
        fiber->m_task();
    }
    fiber->m_state = FiberState::TERM;
    ctx_swap(&(fiber->m_ctx),&(mainFiber->m_ctx));
}


void Fiber::SetThis(ptr co) {
    currentFiber = co;
}
Fiber::ptr Fiber::GetThis(){
    if(currentFiber) return currentFiber;
    else return nullptr;
}
