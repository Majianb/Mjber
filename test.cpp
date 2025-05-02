#include <iostream>
#include <string>
#include <vector>
#include "http_server.h"

int main(){
    
    std::vector<std::pair<std::string,RouteHandler>> a;
    auto func = [](std::shared_ptr<HttpRequest> request)->std::shared_ptr<HttpResponse>{
        std::cout<<"hello"<<std::endl;
        return std::make_shared<HttpResponse>();
    };
    std::string url = "/a";
    a.push_back(std::make_pair(url,func));
    
    HttpServer myserver("127.0.0.1",4000);
    myserver.setRoute(a);
    myserver.setup();

}