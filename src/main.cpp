#include <iostream>
#include <fstream>
#include <filesystem>

#include "../mjber/http_server.h"
#include "../mjber/logger.h"
#include "../mjber/utils.h"
//默认路由
std::shared_ptr<HttpResponse> getIndex(std::shared_ptr<HttpRequest>){
    std::fstream page("../public/index.html");
    std::string content((std::istreambuf_iterator<char>(page)), std::istreambuf_iterator<char>());
    std::shared_ptr<HttpResponse> res = std::make_shared<HttpResponse>();
    res->m_code = 200;
    res->m_version = "HTTP/1.1";
    res->m_reason = "OK";
    res->m_body = std::move(content);
    res->addHeader("Server","mjber-v0.5");
    res->addHeader("Content-Type","text/html");
    res->addHeader("Content-Length", std::to_string(res->m_body.size()));
    return res;
}

// 静态资源
std::shared_ptr<HttpResponse> getPublic(std::shared_ptr<HttpRequest> request){

    std::string path;
    path = "../public" + request->url.substr(request->url.find("public")+6);
    
    
    if (!fileExists(path)){

        LOG_STREAM<<"Failed to open file: "<<path<<ERRORLOG;
        std::shared_ptr<HttpResponse> res = std::make_shared<HttpResponse>();
        res->m_code = 404;
        res->m_version = "HTTP/1.1";
        res->m_reason = "Not Found";
        return res;
    }
    else{
        std::ifstream page(path,std::ios::binary);
        std::string content((std::istreambuf_iterator<char>(page)), std::istreambuf_iterator<char>());
        std::shared_ptr<HttpResponse> res = std::make_shared<HttpResponse>();
        res->m_code = 200;
        res->m_version = "HTTP/1.1";
        res->m_reason = "OK";
        res->m_body = std::move(content);
        res->addHeader("Server","mjber-v0.5");
        res->addHeader("Content-Type","image/jpeg");
        res->addHeader("Content-Length", std::to_string(res->m_body.size()));
        return res;
    }

}


int main(){


    LOG_ADD_CONSOLE_APPENDER();
    auto server = HttpServer("127.0.0.1",4000,1);
    
    RouteRule rule = std::make_pair<std::string,RouteHandler>("/public/*",getPublic);
    RouteRules rules(1,rule);
    server.setRoute(rules);
    
    server.setDefaultHandler(getIndex);
    server.setup();


}