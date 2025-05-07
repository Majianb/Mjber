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
todo fiber的resume应该返回最初start的上下文，而不是返回调用resume的