#include <iostream>
#include "fiber.h"

static int num=1;
void func(int a){
    for(int i =0 ;i<a;i++){
        std::cout<<"F"<<i<<std::endl;
        Fiber::GetThis()->yield();
    }
}
int main(){
    int a=4;
    auto f1 = Fiber::Create(func,a);
    f1->start();
    f1->resume();
    std::cout<<"done"<<std::endl;
}
