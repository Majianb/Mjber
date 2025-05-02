#include <iostream>
#include "../socket_wrapper.h"

int main(){
    auto s = SocketWrapper::Create(SocketWrapper::Type::TCP, "127.0.0.1",4000);
    s->listen();
    auto r = s->accept();
    std::cout<<"yes?";

}