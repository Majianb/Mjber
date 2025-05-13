#include <iostream>
#include "../fiber.h"
#include "../logger.h"

void Func(int a){
    int n = 1;
    std::cout<<"time "<<n<<"from: "<<a<<std::endl;
    Fiber::GetThis()->yield();
    n++;
    std::cout<<"time "<<n<<"from: "<<a<<std::endl;
    throw std::runtime_error("nothing");
}
void callBack(int a){
    std::cout<<"done "<<a<<std::endl;
}
int main(){
    LOG_ADD_CONSOLE_APPENDER();
    auto f1 = Fiber::Create(Func,1);
    auto f2 = Fiber::Create(Func,2);
    f1->setCallBack(callBack,1);
    f2->setCallBack(callBack,2);
    f1->start();
    f2->start();
    f1->resume();
    f2->resume();
    return 0;

}