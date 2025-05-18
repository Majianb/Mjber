#include <iostream>
#include "../timer.h"

void func(int a){
    std::cout<<a<<"begin";
}

int main(){
    Timer mytimer(10);
    auto res = mytimer.addTask(20,func,1);
    while(1){
        int a = 0;
    }
}