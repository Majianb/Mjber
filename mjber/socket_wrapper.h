#ifndef SOCKET_WAPPER
#define SOCKET_WAPPER

#ifdef _WIN32
    #include <ws2tcpip.h>
    #include <windows.h>
    // #pragma comment(lib, "ws2_32.lib")
    #define CLOSE_SOCKET closesocket
#elif defined(__linux__) || defined(__APPLE__)
    #include <sys/socket.h>
    #include <netinet/in.h>
    #include <unistd.h>
    #include <fcntl.h>
    #include <arpa/inet.h>
    #include <sys/un.h>
    #include <netinet/tcp.h>
    #define CLOSE_SOCKET close
#endif

#include <string>
#include <future>
#include <system_error>
#include <cerrno>
#include <cstring>

#include "logger.h"  
#include "scheduler.h"


class SocketWrapper {
public:
    //
    enum class Type { TCP, UDP, Unix };
    
    // 不同平台初始化 windows下需要 WSAStartup/WSACleanup
    struct SocketInitializer {
        SocketInitializer() {
        #ifdef _WIN32
            WSADATA wsaData;
            WSAStartup(MAKEWORD(2,2), &wsaData);
        #endif
        }
        ~SocketInitializer() {
        #ifdef _WIN32
            WSACleanup();
        #endif
        }
    };
    static SocketInitializer globalInitializer;

    // 创建非阻塞的Socket
    static std::shared_ptr<SocketWrapper> Create(Type type, const std::string& addr, uint16_t port = 0) {
        const int domain = GetDomain(addr);
        int socktype = (type == Type::TCP) ? SOCK_STREAM : SOCK_DGRAM;
        int protocol = (type == Type::Unix) ? 0 : (type == Type::TCP ? IPPROTO_TCP : IPPROTO_UDP);

        if (domain == AF_UNIX && type == Type::Unix) {
            socktype = SOCK_STREAM; // UNIX域默认流式套接字
        }

        const int fd = socket(domain, socktype, protocol);
        if (fd == -1) {
            LOG_STREAM<<"Create socket failed: "<<errno<<INFOLOG;
            throw std::system_error(errno, std::system_category());
        }
        // 获取当前文件状态标志
        int flags = fcntl(fd, F_GETFL, 0);
        if (flags == -1) {
            LOG_STREAM<<"Create socket failed: "<<errno<<INFOLOG;
            throw std::system_error(errno, std::system_category());
            close(fd);
        }
        // 设置非阻塞标志
        if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) == -1) {
            LOG_STREAM<<"Create socket failed: "<<errno<<INFOLOG;
            throw std::system_error(errno, std::system_category());
        }

        auto rs = std::make_shared<SocketWrapper>(fd, type, domain);
        rs->bind(addr,port);
        rs->ip_ = addr;
        rs->port_ = port;
        return rs;
    }

    explicit SocketWrapper(int fd, Type type, int domain) 
        : fd_(fd), type_(type), domain_(domain) {}

    ~SocketWrapper() {

        if (fd_ != -1) {
            CLOSE_SOCKET(fd_);
            if(globalScheduler!=nullptr){
                globalScheduler->rmEvent(fd_);
            }
        }
    }

    // 绑定地址（支持IPv4/IPv6/UNIX域）
    bool bind(const std::string& addr, uint16_t port = 0) {
        sockaddr_storage ss{};
        if (!resolveAddress(addr, port, ss)) return false;

        const socklen_t len = (domain_ == AF_INET6) ? sizeof(sockaddr_in6) : 
                            sizeof(sockaddr_in);
        
        if (::bind(fd_, reinterpret_cast<sockaddr*>(&ss), len) == -1) {

            int savedErrno = errno;
            const char* errm = std::strerror(savedErrno);
            std::string m(errm);
            std::cout<<m<<std::endl;
            LOG_STREAM<<"Bind failed: "<<m<<INFOLOG;
            throw std::system_error(errno, std::system_category());
            return false;
        }
        ip_ = addr;
        port_ = port;
        return true;
    }

    // 监听连接,backlog表示监听的最大队列
    bool listen(int backlog = SOMAXCONN) {
        if (type_ != Type::TCP) {
            LOG_STREAM<<"listen() is only available for TCP sockets"<<errno<<ERRORLOG;
            return false;
        }
        if (::listen(fd_, backlog) == -1) {
            LOG_STREAM<<"Listen failed: "<<errno<<ERRORLOG;
            throw std::system_error(errno, std::system_category());
            return false;
        }
        return true;
    }

    // 接受连接，返回一个新的连接，非阻塞
    // 对协程保证返回结果
    std::shared_ptr<SocketWrapper> accept() {
        if (type_ != Type::TCP) {
            LOG_STREAM<<"accept() is only available for TCP sockets"<<ERRORLOG;
            return nullptr;
        }
        sockaddr_storage client_addr{};
        socklen_t client_addr_len = sizeof(client_addr);
        int client_fd;
        while(true){ 
            client_fd = ::accept(fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);
            if (client_fd == -1) {
                auto error_n = errno;
                if(error_n == EAGAIN){ // wait
                    //注册到调度器
                    if(globalScheduler){
                        globalScheduler->addEvent(fd_,EPOLLIN|EPOLLET);
                    }
                    //接收调度,等待可以时会返回
                    if(globalScheduler){
                        globalScheduler->wait();
                    }

                }else{                 // error
                    const char* errorMsgCStr = std::strerror(errno);
                    std::string errorMsg = std::string(errorMsgCStr);
                    LOG_STREAM<<"Accept failed: "<<errorMsg<<ERRORLOG;
                    throw std::runtime_error("Accept failed");
                }
            }else{
                break;
            }
        }
        // 维护地址
        std::string client_ip;
        uint16_t client_port = 0;
        if (domain_ == AF_INET) {
            sockaddr_in* client_addr_in = reinterpret_cast<sockaddr_in*>(&client_addr);
            char ip_str[INET_ADDRSTRLEN];
            inet_ntop(AF_INET, &client_addr_in->sin_addr, ip_str, INET_ADDRSTRLEN);
            client_ip = ip_str;
            client_port = ntohs(client_addr_in->sin_port);
        } else if (domain_ == AF_INET6) {
            sockaddr_in6* client_addr_in6 = reinterpret_cast<sockaddr_in6*>(&client_addr);
            char ip_str[INET6_ADDRSTRLEN];
            inet_ntop(AF_INET6, &client_addr_in6->sin6_addr, ip_str, INET6_ADDRSTRLEN);
            client_ip = ip_str;
            client_port = ntohs(client_addr_in6->sin6_port);
        }
        auto temp = std::make_shared<SocketWrapper>(client_fd, Type::TCP, domain_);
        temp->ip_ = std::move(client_ip);
        temp->port_ = client_port;
        return temp;
    }

    // 异步连接（协程友好）
    std::future<bool> asyncConnect(const std::string& remote, uint16_t port) {
        return std::async(std::launch::async, [=]() {
            sockaddr_storage ss{};
            if (!resolveAddress(remote, port, ss)) return false;

            #ifdef _WIN32
            const int ret = connect(fd_, reinterpret_cast<sockaddr*>(&ss), 
                                (domain_ == AF_INET6) ? sizeof(sockaddr_in6) : 
                                sizeof(sockaddr_in));
            #else
            const int ret = connect(fd_, reinterpret_cast<sockaddr*>(&ss), 
                                (domain_ == AF_UNIX) ? sizeof(sockaddr_un) : 
                                (domain_ == AF_INET6) ? sizeof(sockaddr_in6) : 
                                sizeof(sockaddr_in));
            #endif

            if (ret == 0) return true;

            // 处理非阻塞连接
            const int err = 
            #ifdef _WIN32
                WSAGetLastError();
            #else
                errno;
            #endif
            if (err == EINPROGRESS || err == EWOULDBLOCK) {
                fd_set writefds;
                FD_ZERO(&writefds);
                FD_SET(fd_, &writefds);
                
                timeval timeout{5, 0}; // 5秒超时
                return select(fd_+1, nullptr, &writefds, nullptr, &timeout) > 0;
            }
            return false;
        });
    }

    // 读时直接切到下一协程，等待数据准备完毕后返回

    #ifdef _WIN32
    // win下需要先将句柄注册到端口
    // wsarecv进行异步操作，传入缓存区，关联的IO数据overlapped
    ssize_t read(char* buf,ssize_t len=1024) {

        //注册到调度器
        if(globalScheduler){
            globalScheduler->addEvent(fd_,0,Fiber::GetThis());
        }
    
        WSABUF wsabuf;
        wsabuf.buf = buf;
        wsabuf.len = len;
        DWORD flags = 0; 
        DWORD res=0;
        WSAOVERLAPPED overlapped={0};
        overlapped.hEvent = nullptr;

        // 如果连接已经关闭
        if(fd_ == INVALID_SOCKET){
            return -1;
        }
        // 根据是否启用协程判断是否启用异步
        int r;
        if(globalScheduler) r = WSARecv(fd_,&wsabuf,1,&res,&flags,&overlapped,nullptr);
        else r = WSARecv(fd_,&wsabuf,1,&res,&flags,nullptr,nullptr);
        // 错误处理
        if (r == SOCKET_ERROR) {
            int error = WSAGetLastError();
            if (error == WSAENOTSOCK) {
                // 套接字已被意外关闭
                LOG_STREAM<<"WSARecv failed: "<< error <<ERRORLOG;
                closesocket(fd_);
                return -1;
                // fd_ = INVALID_SOCKET;
            }
            else if(error == ERROR_IO_PENDING){
                //接收调度,等待完成时会返回
                if(globalScheduler){
                    globalScheduler->wait();
                }
                LOG_STREAM<<"fiber "<<std::to_string(Fiber::GetThis()->getID())<<" get resv res "<<std::to_string(Fiber::GetThis()->getIORes())<<DEBUGLOG;
                return Fiber::GetThis()->getIORes();
            }
        }
        return res;
    
    }
    #else
    //linux下的不需要特殊处理,只保证读完
    virtual ssize_t read(char* buf,ssize_t len=1024) {

        // 如果连接已经关闭
        if(fd_ == -1){
            return -1;
        }
        //非阻塞读
        int r;
        while(true){
            r = ::read(fd_, buf, len);
            auto error_n = errno;
            if(r==-1){
                if(error_n == EAGAIN){ // wait
                    //注册到调度器
                    if(globalScheduler){
                        globalScheduler->addEvent(fd_,EPOLLIN|EPOLLERR|EPOLLHUP);
                    }
                    //接收调度,等待可以时会返回
                    if(globalScheduler){
                        globalScheduler->wait();
                    }
                    continue;
                }
                else{                   // error
                    LOG_STREAM<<"Fiber "<<std::to_string(Fiber::GetThis()->getID())<<" soccket read failed: "<< errno <<ERRORLOG;
                    return -1;
                }
            }
            else return r;
        }
    }
    #endif

    #ifdef _WIN32
    //写时也是同样,保证交付len
    size_t write(const char* buf,size_t len) {
        WSABUF wsabuf;
        wsabuf.buf = const_cast<char*>(buf);
        wsabuf.len = len;
        DWORD res = 0;
        DWORD flags = 0; 
        OVERLAPPED overlapped;
        int r = WSASend(fd_,&wsabuf,1,&res,flags,nullptr,nullptr);
        //错误处理

        // if(globalScheduler){
        //     globalScheduler->wait();
        // }
        LOG_STREAM<<"fiber "<<std::to_string(Fiber::GetThis()->getID())<<" get send res "<<std::to_string(res)<<DEBUGLOG;
        return res;
    }
    #else
    //LINUX
    virtual size_t write(const char* buf,size_t len) {
        // LOG_STREAM<<"fiber begin write"<<ERRORLOG;
        // 如果连接已经关闭
        int totol = len;
        if(fd_ == -1){
            return -1;
        }
        // 非阻塞的读
        while(totol>0){
            int r = ::write(fd_, buf, len);
            auto error_n = errno;
            if(r==-1){                 // can not write
                if(error_n == EAGAIN){ // wait
                    //注册到调度器
                    if(globalScheduler){
                        globalScheduler->addEvent(fd_,EPOLLOUT|EPOLLERR|EPOLLHUP);
                    }
                    //接收调度,等待可以时会返回
                    if(globalScheduler){
                        globalScheduler->wait();
                    }
                    continue;
                }
                else{                   // error
                    LOG_STREAM<<"soccket read failed: "<< errno <<ERRORLOG;
                    return -1;
                }
            }
            if(r>=0) totol-=r;
        }
        return 0;
        // LOG_STREAM<<"fiber write resume"<<ERRORLOG;
        
        // LOG_STREAM<<"fiber write: "<<r<<ERRORLOG;
    }
    #endif
    
    std::string& getIP(){
        return ip_;
    }
    uint16_t getPort(){
        return port_;
    }


    // 设置套接字选项==================================

    // 允许地址复用
    void setReuseAddr(bool on) {
        int optval = on ? 1 : 0;
        setsockopt(fd_, SOL_SOCKET, SO_REUSEADDR, 
                  reinterpret_cast<const char*>(&optval), sizeof(optval));
        #ifdef _WIN32
        setsockopt(fd_, SOL_SOCKET, SO_EXCLUSIVEADDRUSE,
                  reinterpret_cast<const char*>(&optval), sizeof(optval));
        #endif
    }
    // 是否禁用naggle算法
    void setTcpNoDelay(bool on) {
        if (type_ == Type::TCP) {
            int optval = on ? 1 : 0;
            setsockopt(fd_, IPPROTO_TCP, TCP_NODELAY,
                      reinterpret_cast<const char*>(&optval), sizeof(optval));
        }
    }

protected:
    //判断地址类型
    static int GetDomain(const std::string& addr) {
        if (addr.find("unix://") == 0) return AF_UNIX;
        return (addr.find(':') != std::string::npos) ? AF_INET6 : AF_INET;
    }

    //解析地址存入字节
    static bool resolveAddress(const std::string& addr, uint16_t port, sockaddr_storage& ss) {
        memset(&ss, 0, sizeof(ss));
        auto domin = GetDomain(addr);
        if (domin == AF_UNIX) {
            #ifdef _WIN32
                return false;//windows不支持
            #else
                sockaddr_un* un = reinterpret_cast<sockaddr_un*>(&ss);
                un->sun_family = AF_UNIX;
                const std::string path = addr.substr(7); // 去除 unix:// 前缀
                strncpy(un->sun_path, path.c_str(), sizeof(un->sun_path)-1);
                return true;
            #endif
        }

        if (domin == AF_INET6) { // IPv6
            sockaddr_in6* sin6 = reinterpret_cast<sockaddr_in6*>(&ss);
            sin6->sin6_family = AF_INET6;
            sin6->sin6_port = htons(port);
            return inet_pton(AF_INET6, addr.c_str(), &sin6->sin6_addr) == 1;
        } else { // IPv4
            sockaddr_in* sin = reinterpret_cast<sockaddr_in*>(&ss);
            sin->sin_family = AF_INET;
            sin->sin_port = htons(port);
            return inet_pton(AF_INET, addr.c_str(), &sin->sin_addr) == 1;
        }
    }

    int fd_ = -1;
    Type type_;
    int domain_;
    bool non_blocking_ = true;
    std::string ip_;
    uint16_t port_;
};

// 静态成员初始化
SocketWrapper::SocketInitializer SocketWrapper::globalInitializer;    


#endif

// ::代表全局作用域的函数

