#ifndef HTTP_SOCKET
#define HTTP_SOCKET
#include <iostream>
#include <unordered_map>
#include <string>
#include <sstream>
#include <vector>
#include <functional>

#include "socket_wrapper.h"


// 首先封装http的请求头
// 当前支持http1.1


/*
    请求行：方法、URL、协议版本
    请求头：键值对
    空行\r\n
    请求体
*/
const std::string HttpM[] = {
    "GET","POST","PUT","DELETE","HEAD","OPTIONS","INVALID_METHOD"
};

class HttpRequest {
public:
    HttpRequest(std::string& m){
        decode(m);
    }
    HttpRequest(){

    };
    std::string getHeader(const std::string& key) const {
        auto it = m_headers.find(key);
        return it != m_headers.end() ? it->second : "";
    }
    void addHeader(const std::string& key,const std::string& value){
        m_headers[key]=value;
    }
    
    std::string encode() const {
        std::ostringstream oss;
        oss << m_method << " " << " " << url << " "<< m_version << "\r\n";
        for (const auto& h : m_headers) 
            oss << h.first << ": " << h.second << "\r\n";
        oss << "\r\n" << m_body;
        return oss.str();
    }
    //解析头部
    int decode(std::string& m){
        int i=0;
        int n=0;
        for(int j=0;j<m.size();j++){
            if(m[j]==' ' || m[j]=='\r'){
                if(n==0){
                    m_method = m.substr(i,j-i);
                    i = j+1;
                    n = 1;
                }
                else if(n==1){
                    url = m.substr(i,j-i);
                    i = j+1;
                    n = 2;
                }
                else if(n==2){
                    m_version = m.substr(i,j-i);
                    i = j+2;
                    n = 3;
                    break;
                }
            }
        }

        int vflag = 0;
        std::string key;
        std::string value;
        for(int j=i;j<m.size();j++){
            if (m[j]==':'){
                key = m.substr(i,j-i);
                i = j+2;
                vflag = 1;
            }
            else if(m[j]=='\r' && vflag){
                value = m.substr(i,j-i);
                i = j+2;
                vflag = 0;
            }
            else if(m[j]=='\r' && m[j+1]=='\n'){
                break;
            }
            m_headers[key] = value;
        }

        //根据头部信息解析消息体
        //支持json等格式


        return 0;
    }

    std::string url;
    std::unordered_map<std::string, std::string> m_headers; //头部各个值
    std::string m_body; 
    std::string m_version;
    std::string m_method;

};

//响应消息
/*
    状态行：协议版本、状态码、原因
    响应头：键值对
    响应体：
*/
/*
    状态码：200 成功、404 失败
    响应头：
        Server：表示处理请求的服务器软件名称和版本。
        Content-Type：指定响应体的媒体类型，例如 text/html 表示 HTML 文档，application/json 表示 JSON 数据。
        Content-Length：指定响应体的长度（以字节为单位）。
        Cache-Control：用于控制缓存策略，例如 no-cache 表示不使用缓存。
*/
class HttpResponse {
public:
    std::string getHeader(const std::string& key) const {
        auto it = m_headers.find(key);
        return it != m_headers.end() ? it->second : "";
    }
    void addHeader(const std::string& key,const std::string& value){
        m_headers[key]=value;
    }
    
    // 序列化为符合RFC标准的响应字符串
    std::string encode() const {
        std::ostringstream oss;
        oss << m_version << " " << static_cast<int>(m_code) << " " << m_reason << "\r\n";
        for (const auto& h : m_headers) 
            oss << h.first << ": " << h.second << "\r\n";
        oss << "\r\n" << m_body;
        return oss.str();
    }
    // 解析响应
    int decode(std::string& m){
        int i=0;
        int n=0;
        for(int j=0;j<m.size();j++){
            if(m[j]==' ' || m[j]=='\r'){
                if(n==0){
                    m_version = m.substr(i,j-i);
                    i = j+1;
                    n = 1;
                }
                else if(n==1){
                    m_code = 0;
                    for(int k=i;k<j;k++){
                        m_code = m_code*10 + m[k]-'0';
                    }
                    i = j+1;
                    n = 2;
                }
                if(n==2){
                    m_reason = m.substr(i,j-i);
                    i = j+2;
                    n = 3;
                    break;
                }
            }
        }
        std::string key;
        std::string value;
        for(int j=i;j<m.size();j++){
            if(m[j]=='\r' && m[j+1]=='\n'){
                break;
            }
            else if (m[j]==' '){
                key = m.substr(i,j-i-1);
                i = j+1;
            }
            else if(m[j]=='\r'){
                value = m.substr(i,j-i);
                i = j+2;
            }
            m_headers[key] = value;
        }

        //下一步解析内容

        return 0;
    }

    std::unordered_map<std::string, std::string> m_headers; //头部各个值
    std::string m_body; //消息体
    std::string m_version;//版本
    int m_code;//状态码
    std::string m_reason;//原因
};


/*
    进一步封装socket来接收完整的http消息，管理一次完整的消息传输
    todo: 充分利用管道化，支持对多个请求并行准备应答
*/

class HttpScoket{
public:
    HttpScoket(std::shared_ptr<SocketWrapper> socket):socket(socket){};
    int readRequest(std::shared_ptr<HttpRequest> request); //接收请求
    int writeResponse(std::shared_ptr<HttpResponse> response); //发送响应
    size_t write(void* p,size_t len); //发送消息

private:
    std::shared_ptr<SocketWrapper> socket;
};

//发送
int HttpScoket::writeResponse(std::shared_ptr<HttpResponse> response){
    std::string m = response->encode();
    return socket->write(m.c_str(),m.size());
}

//接收,保证接收到完整
int HttpScoket::readRequest(std::shared_ptr<HttpRequest> request){
    
    char* buf = new char[1024];
    std::string m;
    size_t totol = 0;
    while(true){
        size_t mlen = socket->read(buf,1024);
        if(mlen<=0) break;
        m.append(buf,mlen);
        //每次新的数据进行查找
        size_t end_pos = m.find("\r\n\r\n",totol);
        if(end_pos != std::string::npos){ //找到的话先把请求头都读取
            std::string m_head = m.substr(0,end_pos+4);
            request->decode(m_head);
            
            //如果还带着消息体，应该继续接收
            if(request->m_method=="POST" && request->getHeader("Content-Length")!=""){
                size_t content_len = stoi(request->getHeader("Content-Length"));
                std::string context = m.substr(m_head.size(),m.size()-m_head.size()); //存消息体
                while(content_len>0){
                    size_t mlen = socket->read(buf,1024);
                    if(mlen<0) break;
                    else{
                        context.append(buf,mlen);
                        content_len-=mlen;
                    }
                }
                if(content_len==0){
                    request->m_body = std::move(context);
                    break;
                }
                //否则报错
            }
            else break;

        }
        else totol+=mlen;
    }
    delete[] buf;
    return 0;
}


#endif
