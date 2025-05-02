#ifndef HTTP_SERVER
#define HTTP_SERVER

#include <iostream>
#include <functional>
#include <vector>
#include <unordered_map>
#include <map>
#include <utility>

#include "scheduler.h"
#include "socket_wrapper.h"
#include "http_socket.h"



/* 
    路由表，根据路径，组织为一个树
    使用分层的map
    第一层默认为空
*/
// 处理函数接收请求返回响应
typedef std::function<std::shared_ptr<HttpResponse>(std::shared_ptr<HttpRequest>)> RouteHandler;

// 每一层结构w为一个处理函数，可以获得请求消息的指针
class RouteTreeNode{
public:
    RouteTreeNode(RouteHandler routeFunc):handler(routeFunc){};
    RouteTreeNode(){};
    std::map<std::string,std::shared_ptr<RouteTreeNode>> nextTable;
    int level;
    RouteHandler handler;
};
    
// 组织成树结构
class RouteTree{
public:
    //构造，输入url和对应的处理函数
    RouteTree(){
        head = std::make_shared<RouteTreeNode>();
    }
    RouteTree(std::vector<std::pair<std::string,RouteHandler>> url_handlers){
        head = std::make_shared<RouteTreeNode>();
        int level = 0;

        // 对每一个url
        for(int i=0;i<url_handlers.size();i++){
            std::shared_ptr<RouteTreeNode> temp=head;
            level = 0;
            size_t lastpos=1;
            std::pair<std::string,RouteHandler> url_handler = url_handlers[i];
            std::string url = url_handler.first;
            RouteHandler handler = url_handler.second;
            while(lastpos!=std::string::npos){ //不断向下插入新的
                size_t pos = url.find('/',lastpos);
                std::string suburl;
                if(pos==std::string::npos)
                    suburl = url.substr(lastpos);
                else
                    suburl = url.substr(lastpos,pos-lastpos);
                lastpos = pos;
                //查找是否有字段，有则进入
                if(temp->nextTable.find(suburl)!=temp->nextTable.end()){
                    temp = temp->nextTable.find(suburl)->second;
                    continue;
                }
                else{ //没有时应该创建新节点
                    std::shared_ptr<RouteTreeNode> newnode = std::make_shared<RouteTreeNode>(handler);
                    temp->nextTable.insert(make_pair(suburl,newnode));
                    temp = newnode;
                    continue;
                }
            } 
        }
    }
    //根据路径查找处理函数
    RouteHandler find(const std::string& url){
        auto temp = head;
        size_t pos,lastpos;
        RouteHandler candidateFunc = defaultHandler;
        if(url=="") return candidateFunc;
        lastpos = 1;
        while(lastpos!=std::string::npos){
            std::string suburl;
            pos = url.find('/',lastpos);
            if(pos==std::string::npos)
                suburl = url.substr(lastpos);
            else
                suburl = url.substr(lastpos,pos-lastpos);
            lastpos = pos;
            // then reseach
            // if we can
            if(temp->nextTable.find(suburl)!=temp->nextTable.end()){
                temp = temp->nextTable.find(suburl)->second;
                continue;
            }
            else{ //otherwise use default
                if(temp->nextTable.find("*")!=temp->nextTable.end())
                    return temp->nextTable.find("*")->second->handler;
                else
                    return candidateFunc;
            }

        }
        return temp->handler;
    }

    void setDefaultHandler(RouteHandler handler){
        defaultHandler = handler;
    }

private:
    std::shared_ptr<RouteTreeNode> head;
    RouteHandler defaultHandler;
};



/*
    http服务器框架
*/

class HttpServer{
public:
    
    HttpServer(const std::string& addr,uint16_t port,int thread_num);
    ~HttpServer(){};
    // 接口
    int setup(); //启动
    int setRoute(std::vector<std::pair<std::string,RouteHandler>> url_handlers); //设置路由
    
private:

    std::vector<SocketWrapper> clients; //用户的连接
    std::shared_ptr<SocketWrapper> serverSocket; 
    std::shared_ptr<FiberScheduler> scheduler;
    RouteHandler defaultHandler; //默认路由的处理
    RouteTree  routeTable; //路由表
    static void worker(HttpServer* p, std::shared_ptr<SocketWrapper> socket); //处理流程
};

HttpServer::HttpServer(const std::string& addr,uint16_t port,int thread_num=-1):routeTable(){
    // 1. 构建缺省路由处理
    defaultHandler = [](std::shared_ptr<HttpRequest> request)->std::shared_ptr<HttpResponse>{
        auto res = std::make_shared<HttpResponse>();
        res->m_code = 200;
        res->m_version = "HTTP/1.1";
        res->m_reason = "OK";
        res->m_body = "<h1>nothing is in here-_-</h1><h2>from mjber-v0.5 by mjb</h2>";
        res->addHeader("Server","mjber-v0.5");
        res->addHeader("Content-Type","text/html");
        res->addHeader("Content-Length", std::to_string(res->m_body.size()));
        return res;
    };
    routeTable.setDefaultHandler(defaultHandler);

    // 2. 构建socket
    serverSocket = SocketWrapper::Create(SocketWrapper::Type::TCP,addr,port);
    // 3. 初始化调度器
    if(thread_num>0) globalScheduler = std::make_shared<FiberScheduler>(thread_num);
    
}

int HttpServer::setRoute(std::vector<std::pair<std::string,RouteHandler>> url_handlers){
    auto routeTable = RouteTree(url_handlers);
    routeTable.setDefaultHandler(defaultHandler);
    return 1;
}

// server对请求的的处理流程
void HttpServer::worker(HttpServer* p, std::shared_ptr<SocketWrapper> c_socket){
    // 1.先接收消息
    auto  httpsocket = std::make_shared<HttpScoket>(c_socket);
    std::shared_ptr<HttpRequest> request = std::make_shared<HttpRequest>();
    httpsocket->readRequest(request);
    // 2.根据路由进行下一步的操作
    RouteHandler handler = p->routeTable.find(request->url);
    auto res = handler(request);
    // 3.返回响应
    httpsocket->writeResponse(res);
}


// server的启动流程
int HttpServer::setup(){
    // 1.监听
    serverSocket->listen();
    // 2.循环接收连接
    while(true){
        auto newClient =  serverSocket->accept();
        //3.对于每一个到来的连接分配一个协程去执行对应操作
        if(newClient==nullptr){
            throw std::runtime_error("Failed to accept");
        }
        else{
            LOG_STREAM<<"Get a connect: "<<INFOLOG;
            if(scheduler) scheduler->addTask(worker,this,newClient);
            else worker(this,newClient);
        }

    }   
}
#endif 