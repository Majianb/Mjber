#include <chrono>
#include <thread>
#include <iostream>
#include <functional>
#include <future>
#include <map>
#include <vector>

/*
    实现简易的计时器，提供固定时间间隔的回调
    维护一个任务列表,map
*/

class Timer{
public:

    static std::shared_ptr<Timer> globalTimer;

    Timer(int v=10);
    ~Timer(){};
    template<typename F,typename... Args>
    std::future<typename std::result_of<F(Args...)>::type> addTask(int w_time,F f,Args... args);
private:
    void myFunc(); // 工作流程

    int v; //最小间隔
    std::thread worker;
    std::map<long long,std::function<void()>> tasks;
    std::mutex map_mutex;
};

std::shared_ptr<Timer> Timer::globalTimer = std::make_shared<Timer>(4);

Timer::Timer(int time_v){
    v = time_v;
    worker = std::thread([this](){this->myFunc();});
};

template<typename F,typename... Args>
std::future<typename std::result_of<F(Args...)>::type> Timer::addTask(int w_time,F f,Args... args){
    if(w_time<=v) w_time = v;
    else w_time = w_time/v * v + v;
    using return_type = typename std::result_of<F(Args...)>::type;
    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );
    std::future<return_type> res = task->get_future();
    //包装为无返回值无参数
    auto f_task = [](std::shared_ptr<std::packaged_task<return_type()>> o_task){
        (*o_task)();
    };

    //获取时间戳
    long long n_time =  std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch()
    ).count();
    //唤醒时间
    long long f_time = n_time + w_time;
    //插入任务表
    {
        std::lock_guard<std::mutex> lock(map_mutex);
        tasks[f_time] = std::bind(f_task,task);
    }
    return res;
}

void Timer::myFunc(){
    // 循环sleep来唤醒任务
    while(true){
        // 1. 获取时间戳
        auto pointer = tasks.end();
        std::vector<std::function<void()>> temp;
        long long c_time =  std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::system_clock::now().time_since_epoch()
        ).count();


        // 2. 获取待唤醒任务
        {
            std::lock_guard<std::mutex> lock(map_mutex);
            if(!tasks.empty()){
                pointer = tasks.upper_bound(c_time);
                for(auto start=tasks.begin();start != pointer;){ // 注意这里没有start++
                    temp.push_back(start->second);
                    start = tasks.erase(start); // 正确的删除方式，erase返回下一个迭代器
                }
            }
        }
        // 3. 唤醒任务
        for(int i=0;i<temp.size();i++){
            auto task = temp[i];
            task();
        } 
        temp.clear();
        // 4. 阻塞自己
        std::this_thread::sleep_for(std::chrono::milliseconds(v));
    }
}