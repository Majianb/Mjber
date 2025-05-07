#include <iostream>
#include "../fiber.h"

void Func(int a){
    int n = 1;
    std::cout<<"time"<<n<<"from: "<<a<<std::endl;
    Fiber::GetThis()->yield();
    n++;
    std::cout<<"time"<<n<<"from: "<<a<<std::endl;
}

int main(){
    auto f1 = Fiber::Create(Func,1);
    auto f2 = Fiber::Create(Func,2);
    f1->start();
    f2->start();
    f1->resume();
    f2->resume();
    return 0;

}