#include <iostream>
#include <string>
#ifdef _WIN32
    #include <windows.h>
#else
    #include <sys/stat.h>
#endif

/*
    查找文件是否存在
    后续应该加入分布式的改进
*/
bool fileExists(const std::string& path) {
    #ifdef _WIN32
        DWORD attributes = GetFileAttributesA(path.c_str());
        return (attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY));
    #else
        struct stat buffer;
        return (stat(path.c_str(), &buffer) == 0);
    #endif
}