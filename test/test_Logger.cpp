#include <iostream>

#include "../logger.h"

int main(){
    LOG_ADD_CONSOLE_APPENDER();
    // LOG_INFO<<"test stream"<<" yes";
    LogDEBUG("222");
    LOG_STREAM<<"JUST TEST"<<"1"<<INFOLOG;
    LOG_STREAM<<"JUST TEST"<<"2"<<ERRORLOG;
    LOG_STREAM<<"JUST TEST"<<"3"<<ERRORLOG;
    LOG_STREAM<<"JUST TEST"<<"4"<<ERRORLOG;
    LOG_STREAM<<"JUST TEST"<<"5"<<ERRORLOG;
    while(true){}
}