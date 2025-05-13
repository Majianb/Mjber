#ifndef SSL_SOCKET_WRAPPER
#define SSL_SOCKET_WRAPPER
#include <string>
#include <openssl/ssl.h>
#include <openssl/err.h>

#include "socket_wrapper.h"
/*
    封装SSL SOCKET提供加密连接
*/

class SSLSocketWrapper:public SocketWrapper{
public:
    struct SSLInitializer
    {
        SSL_CTX* ssl_ctx;
        SSLInitializer(const std::string& crt_path,const std::string& privatekey_path){
            SSL_library_init();
            OpenSSL_add_all_algorithms();
            SSL_load_error_strings();
            ssl_ctx = SSL_CTX_new(TLS_server_method());
            SSL_CTX_use_certificate_file(ssl_ctx,crt_path.c_str(),SSL_FILETYPE_PEM);
            SSL_CTX_use_PrivateKey_file(ssl_ctx,privatekey_path.c_str(),SSL_FILETYPE_PEM);
            // 验证私钥与证书是否匹配
            if (!SSL_CTX_check_private_key(ssl_ctx)) {
                fprintf(stderr, "Private key does not match the public certificate\n");
                exit(EXIT_FAILURE);
            }
        }
        ~SSLInitializer(){
            EVP_cleanup();
            ERR_free_strings();
        }
    };
    static std::shared_ptr<SSLInitializer> ssl_initializer;

    explicit SSLSocketWrapper(int fd, SSL* ssl,Type type, int domain,
            const std::string& crt_path = "./server.crt",
            const std::string& key_path="./server.key")
    :SocketWrapper(fd,type,domain),ssl(ssl){
        if(ssl_initializer==nullptr)
            ssl_initializer = std::make_shared<SSLInitializer>(crt_path,key_path);
        if(ssl==nullptr) ssl = SSL_new(ssl_initializer->ssl_ctx);
        SSL_set_fd(ssl,fd_);
    }
    static std::shared_ptr<SSLSocketWrapper> Create();
    std::shared_ptr<SSLSocketWrapper> connect();  
    std::shared_ptr<SSLSocketWrapper> accept();
    virtual size_t read(char* buf,size_t len);
    virtual size_t write(char* buf,size_t len);

private:
    SSL* ssl;

};

std::shared_ptr<SSLSocketWrapper::SSLInitializer> SSLSocketWrapper::ssl_initializer = nullptr;

std::shared_ptr<SSLSocketWrapper> SSLSocketWrapper::accept(){
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
                LOG_STREAM<<"ssl Accept failed: "<<errorMsg<<ERRORLOG;
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
    // ssl设置
    SSL* c_ssl = SSL_new(ssl_initializer->ssl_ctx);
    SSL_set_fd(c_ssl, client_fd);
    // 执行TLS握手
    while(true){ 
        auto tls_res = SSL_accept(c_ssl);
        int error_n = SSL_get_error(c_ssl, tls_res);
        if (client_fd <= 0) {
            if(error_n == SSL_ERROR_WANT_READ){ // wait
                //注册到调度器
                if(globalScheduler){
                    globalScheduler->addEvent(fd_,EPOLLIN|EPOLLET|EPOLLHUP);
                }
                //接收调度,等待可以时会返回
                if(globalScheduler){
                    globalScheduler->wait();
                }

            }else if(error_n == SSL_ERROR_WANT_WRITE){
                if(globalScheduler){
                    globalScheduler->addEvent(fd_,EPOLLOUT|EPOLLET|EPOLLHUP);
                }
                //接收调度,等待可以时会返回
                if(globalScheduler){
                    globalScheduler->wait();
                }
            }
            else{                 // error
                LOG_STREAM<<"ssl Accept failed: "<<error_n<<ERRORLOG;
                throw std::runtime_error("Accept failed");
            }
        }else{
            break;
        }
    }
    auto temp = std::make_shared<SSLSocketWrapper>(client_fd,c_ssl,Type::TCP, domain_);
    temp->ip_ = std::move(client_ip);
    temp->port_ = client_port;
    return temp;
}

size_t SSLSocketWrapper::read(char* buf,size_t len){
    // 如果连接已经关闭
    if(fd_ == -1){
        return -1;
    }
    //非阻塞读
    int r;
    while(true){
        r = ::SSL_read(ssl, buf, len);
        int error_n = SSL_get_error(ssl, r);
        if(r==-1){
            if(error_n == SSL_ERROR_WANT_READ){ // wait
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
                LOG_STREAM<<"Fiber "<<std::to_string(Fiber::GetThis()->getID())<<" ssl soccket read failed: "<< error_n <<ERRORLOG;
                return -1;
            }
        }
        else return r;
    }
}

size_t SSLSocketWrapper::write(char* buf,size_t len){
    // 如果连接已经关闭
    size_t totol = len;
    if(fd_ == -1){
        return -1;
    }
    // 非阻塞的读
    while(totol>0){
        int r = SSL_write(ssl, buf, len);
        int error_n = SSL_get_error(ssl, r);
        if(r==-1){                 // can not write
            if(error_n == SSL_ERROR_WANT_CONNECT){ // wait
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
                LOG_STREAM<<"Fiber "<<std::to_string(Fiber::GetThis()->getID())<<" ssl soccket write failed: "<< error_n <<ERRORLOG;
                return -1;
            }
        }
        if(r>=0) totol-=r;
    }
    return 0;
}

#endif