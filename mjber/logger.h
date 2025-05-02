#ifndef LOGGER
#define LOGGER
#include <iostream>
#include <vector>
#include <fstream>
#include <sstream>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

// 设置
#define BUFFER_SIZE 1024




// 日志级别
enum LogLevel{
    DEBUG,
    INFO,
    WARN,
    LOGERROR,
    FATAL
};
#define DEBUGLOG LogLevel::DEBUG
#define INFOLOG LogLevel::INFO
#define WARNLOG LogLevel::WARN
#define ERRORLOG LogLevel::LOGERROR
#define FATALLOG LogLevel::FATAL


// 日志记录结构体
struct LogEvent {
    LogLevel level;
    std::string message;
    std::time_t timestamp;

    LogEvent(LogLevel l, const std::string& msg);
    LogEvent() = default;
};

// 日志输出器
class LogAppender {
public:
    virtual void append(const LogEvent& event) = 0;
    virtual ~LogAppender() = default;
};

// 控制台输出器
class ConsoleAppender : public LogAppender {
public:
    void append(const LogEvent& event) override;
};

// 文件输出器
class FileAppender : public LogAppender {
public:
    FileAppender(const std::string& filename);
    void append(const LogEvent& event) override;
    ~FileAppender();

private:
    std::ofstream file;
};


// 日志器类
// 单例模式,禁止拷贝和复制
class Logger {
public:
    static Logger& getLogger(){
        static Logger instance;
        return instance;
    };

    void addAppender(std::shared_ptr<LogAppender> appender);
    void log(LogLevel level, const std::string& message);
    Logger(const Logger&) = delete;
    Logger& operator=(const Logger&) = delete;

private:
    std::vector<std::shared_ptr<LogAppender>> appenders;
    class LoggerImpl;
    std::unique_ptr<LoggerImpl> impl;
    Logger();
    ~Logger();
    // static constexpr size_t BUFFER_SIZE = 1024; // 编译时求值
    


};

//实现
// 日志记录结构体，生成一个条目
LogEvent::LogEvent(LogLevel l, const std::string& msg) : level(l), message(msg) {
    timestamp = std::time(nullptr);
}

// 环形缓冲区
template <size_t N>
class RingBuffer {
public:
    RingBuffer() : head(0), tail(0), count(0) {}

    bool push(const LogEvent& event) {
        std::unique_lock<std::mutex> lock(mutex);
        if (count == N) {
            // 缓冲区已满，覆盖旧的日志
            head = (head + 1) % N;
        } else {
            ++count;
        }
        buffer[tail] = event;
        tail = (tail + 1) % N;
        return true;
    }

    bool pop(LogEvent& event) {
        std::unique_lock<std::mutex> lock(mutex);
        if (count == 0) {
            return false;
        }
        event = buffer[head];
        head = (head + 1) % N;
        --count;
        return true;
    }
    bool empty(){
        return count==0;
    }
private:
    LogEvent buffer[N];
    size_t head;
    size_t tail;
    size_t count;
    std::mutex mutex;
};

// 控制台输出器实现
void ConsoleAppender::append(const LogEvent& event) {
    std::stringstream ss;
    ss << std::ctime(&event.timestamp) << " - " << [](LogLevel level) {
        switch (level) {
            case LogLevel::DEBUG: return "DEBUG";
            case LogLevel::INFO: return "INFO";
            case LogLevel::WARN: return "WARN";
            case LogLevel::LOGERROR: return "ERROR";
            case LogLevel::FATAL: return "FATAL";
            default: return "UNKNOWN";
        }
    }(event.level) << " - " << event.message << std::endl;
    std::cout << ss.str();
}

// 文件输出器实现
FileAppender::FileAppender(const std::string& filename) : file(filename, std::ios::app) {}

void FileAppender::append(const LogEvent& event) {
    if (file.is_open()) {
        std::stringstream ss;
        ss << std::ctime(&event.timestamp) << " - " << [](LogLevel level) {
            switch (level) {
                case LogLevel::DEBUG: return "DEBUG";
                case LogLevel::INFO: return "INFO";
                case LogLevel::WARN: return "WARN";
                case LogLevel::LOGERROR: return "ERROR";
                case LogLevel::FATAL: return "FATAL";
                default: return "UNKNOWN";
            }
        }(event.level) << " - " << event.message << std::endl;
        file << ss.str();
    }
}

FileAppender::~FileAppender() {
    if (file.is_open()) {
        file.close();
    }
}


// 日志器实现类
// 使用pimpl模式，将实现类与接口类分离
class Logger::LoggerImpl {
public:
    LoggerImpl(std::vector<std::shared_ptr<LogAppender>>& appenders)
        :appenders(appenders), stop(false) {
        buffer = std::make_unique<RingBuffer<BUFFER_SIZE>>();
        worker = std::thread(&LoggerImpl::asyncWrite, this);
    }

    ~LoggerImpl() {
        {
            std::unique_lock<std::mutex> lock(mutex);  // 加锁
            stop = true;
        }
        cv.notify_one();
        if (worker.joinable()) {
            worker.join();
        }
    }

    void notify() {
        cv.notify_one();
    }
    void push(const LogEvent& event){
        buffer->push(event);
    }

private:
    std::unique_ptr<RingBuffer<BUFFER_SIZE>> buffer;// 缓冲区
    std::vector<std::shared_ptr<LogAppender>>& appenders;// 输出器列表
    std::mutex mutex; 
    std::condition_variable cv;  // 通知线程处理
    std::thread worker;
    std::atomic<bool> stop;

    // 异步写日志
    void asyncWrite() {
        LogEvent event;
        bool getflag = false;
        while (true) {
            {

                std::unique_lock<std::mutex> lock(mutex);

                cv.wait(lock, [this,&event,&getflag]()->bool{ 
                    getflag = buffer->pop(event);
                    return getflag || stop;
                }); //取出一个条目或停止时返回
            }
            if(getflag){
                for (auto& appender : appenders) {
                    appender->append(event);
                }
                getflag = false;
            }
            else if (stop && buffer->empty()) { 
                break;
            }
        }
    }
};






// 日志器构造函数
Logger::Logger() : impl(nullptr) {
    impl = std::make_unique<LoggerImpl>(appenders);
}

// 日志器析构函数
Logger::~Logger() = default;

// 添加输出器
void Logger::addAppender(std::shared_ptr<LogAppender> appender) {
    appenders.push_back(appender);
}

// 记录日志
void Logger::log(LogLevel level, const std::string& message) {
    impl->push(LogEvent(level, message));
    impl->notify();
}


//管理接口
#define LOG_ADD_CONSOLE_APPENDER() LOG.addAppender(std::make_shared<ConsoleAppender>())
#define LOG_ADD_FILE_APPENDER(filename) LOG.addAppender(std::make_shared<FileAppender>(filename))

//普通调用接口
#define LOG Logger::getLogger()
#define LogDEBUG(message) LOG.log(LogLevel::DEBUG, message)
#define LogINFO(message) LOG.log(LogLevel::INFO, message)
#define LogWARN(message) LOG.log(LogLevel::WARN, message)
#define LogERROR(message) LOG.log(LogLevel::LOGERROR, message)
#define LogFATAL(message) LOG.log(LogLevel::FATAL, message)

//实现流式调用
class LogStream{
public:
    std::stringstream ss;
    LogStream(){};

    template <typename T>
    LogStream& operator<<(T& t){
        ss<<t;
        return *this;
    }
    void operator<<(LogLevel level){
        auto res = ss.str();
        ss.str("");
        LOG.log(level,res);
    }
    static LogStream& getLogStream(){
        static LogStream log_stream;
        return log_stream;
    }
    
};
 

//流式接口
#define LOG_STREAM LogStream::getLogStream()


#endif

// constexpr 表示编译时求值
// 虚析构用于防止基类指针调用基类析构函数
// ring log基本原理，使用一个环形的缓冲区
//