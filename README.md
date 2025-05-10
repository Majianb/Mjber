# mjber
精简的跨平台分布式协程高性能服务器框架

### 2025-5-4 基本完成简单的htpp服务器
### 2025-5-5 windows和linux调试
### 2025-5-6 
todo logeer.h的文件输出调试,现在不能输出内容   
todo scheduler.h和socket封装改进，对于输出，可以使用非阻塞，先write,如果发送失败，再注册epoll。windows同样可以如此改进。    
todo 加入ssl      
todo 退出时清理资源,清理调度器的注册表，清理协程。    
todo 出错处理
### 2025-5-7 linux平台下的调度器的清理和协程管理     
- [ ] ~~ fiber的resume应该返回最初start的上下文，而不是返回调用resume的 --------X,resume回去最初的start会导致过早执行 ~~    
### 2025-5-8 
- [ ] 笔记：对于socket，可以通过 fcntl(fd, F_SETFL, flags | O_NONBLOCK) 来设置非阻塞   
    对于非阻塞的操作，linux会通过errno设置为EAGAIN error again 来提示下一步或许就能成功（windows则 为              EWOULDBLOCK= error would block）   
    当然对于read这种直接返回0表示已经无可读了        
- [x] linux的socket改成了非阻塞的方式  
- [x] 服务器的socket的accpet会直接返回了,改了linux平台的accept   
- [ ] 更安全的fiber,主线程调用fiber相关会直接错误   
### 2025-5-9
- [x] 用户协程的出错管理，出错后清理结束   
- [x] 协程 start进入工作函数，调用yeild会回到start后一条语句，对于resume也是同样，因此调度器的清理逻辑不应该在start后，而应该作为fiber的回调函数      
### 2025-5-10  
    - [x] fiber添加结束的回调   
    - [x] 调度器添加fiber结束的处理   
- [ ] 调整套接字和协程，调度器的资源回收    