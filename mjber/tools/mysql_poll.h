#ifndef SQLCONNECTIONPOLL
#define SQLCONNECTIONPOLL
#include <mysql/jdbc.h>
#include <memory>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <string>
#include <stdexcept>
#include <atomic>


/*
sql::Driver* driver = get_driver_instance(); 驱动管理
connect(url, user, password) 建立连接
createStatement()
prepareStatement(sql)
executeQuery(sql) 执行查询语句（如 SELECT），返回 ResultSet 对象。
executeUpdate(sql) 执行更新语句（如 INSERT、UPDATE、DELETE），返回受影响的行数。
结果集相关
getInt(column)、getString(column)、getDouble(column)

*/



/**
 * 实现一个mysql的连接池
 *  1. 连接维护到一个队列
 *  2. 连接的检查维护，心跳检测
 *  3. 负载均衡
 * 
 * 
 */
class MySQLConnectionPool {
private:
    // 数据库连接信息
    std::string host_;
    std::string user_;
    std::string password_;
    std::string database_;
    int port_;
    
    // 连接池配置
    size_t minConnections_;  // 最小连接数
    size_t maxConnections_;  // 最大连接数
    size_t timeout_;         // 获取连接超时时间(毫秒)
    
    // 连接池状态
    std::queue<std::unique_ptr<sql::Connection>> connections_;  // 空闲连接队列
    std::atomic<size_t> activeConnections_;                      // 当前活跃连接数
    std::mutex poolMutex_;                                       // 保护连接池的互斥锁
    std::condition_variable cv_;                                 // 用于等待连接的条件变量
    std::atomic<bool> initialized_;                              // 连接池初始化状态
    
    // 禁止拷贝构造和赋值
    MySQLConnectionPool(const MySQLConnectionPool&) = delete;
    MySQLConnectionPool& operator=(const MySQLConnectionPool&) = delete;

public:
    /**
     * 构造函数
     * @param host 数据库主机地址
     * @param user 用户名
     * @param password 密码
     * @param database 数据库名
     * @param port 端口号，默认3306
     * @param minConnections 最小连接数，默认5
     * @param maxConnections 最大连接数，默认20
     * @param timeout 获取连接超时时间(毫秒)，默认3000
     */
    MySQLConnectionPool(
        const std::string& host,
        const std::string& user,
        const std::string& password,
        const std::string& database = "",
        int port = 3306,
        size_t minConnections = 5,
        size_t maxConnections = 20,
        size_t timeout = 3000
    ) : host_(host),
        user_(user),
        password_(password),
        database_(database),
        port_(port),
        minConnections_(minConnections),
        maxConnections_(maxConnections),
        timeout_(timeout),
        activeConnections_(0),
        initialized_(false) {}
    
    /**
     * 析构函数，关闭并释放所有连接
     */
    ~MySQLConnectionPool() {
        shutdown();
    }
    
    /**
     * 执行select命令
     * 使用prepareStatement
     * 参数占有符
     *  int %d
     *  string %s
     *  double %f
     *  long   %l
     */
    std::unique_ptr<sql::ResultSet> executeQuery(const std::string& command) {
        try{
            auto con = getConnection();
            std::unique_ptr<sql::Statement> stmt(con->createStatement());
            return std::unique_ptr<sql::ResultSet>(stmt->executeQuery(command));
        } catch (const sql::SQLException& e) {
            throw std::runtime_error("fail to SQL querry: " + std::string(e.what()));
        }
    }
    template<typename ...Args>
    std::unique_ptr<sql::ResultSet> executeQuery(const std::string& command, Args... args) {
        // 从连接池获取连接
        auto con = getConnection();
        
        // 处理SQL命令中的占位符，转换为?
        std::string preparedSql = command;
        size_t pos = 0;
        int paramCount = 0;
        
        // 统计参数数量并替换占位符为?
        while ((pos = preparedSql.find("%d", pos)) != std::string::npos) {
            preparedSql.replace(pos, 2, "?");
            paramCount++;
        }
        
        pos = 0;
        while ((pos = preparedSql.find("%s", pos)) != std::string::npos) {
            preparedSql.replace(pos, 2, "?");
            paramCount++;
        }
        
        pos = 0;
        while ((pos = preparedSql.find("%f", pos)) != std::string::npos) {
            preparedSql.replace(pos, 2, "?");
            paramCount++;
        }
        
        pos = 0;
        while ((pos = preparedSql.find("%l", pos)) != std::string::npos) {
            preparedSql.replace(pos, 2, "?");
            paramCount++;
        }
        
        // 检查参数数量是否匹配
        if (paramCount != sizeof...(args)) {
            throw std::invalid_argument("wrong args nums!");
        }
        
        try {
            // 创建预处理语句
            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(preparedSql));
            
            // 绑定参数
            int index = 1;
            bindParameters(pstmt.get(), index, args...);
            
            // 执行查询并返回结果集
            return std::unique_ptr<sql::ResultSet>(pstmt->executeQuery());
        } catch (const sql::SQLException& e) {
            throw std::runtime_error("fail to SQL querry: " + std::string(e.what()));
        }
    }

    
    /**
     * 执行update/insert命令
     * 使用prepareStatement
     * 参数占有符
     *  int %d
     *  string %s
     *  double %f
     *  long   %l
     */
    template<typename ...Args>
    int executeUpdate(const std::string& command, Args... args){
        // 从连接池获取连接
        auto con = getConnection();
        
        // 处理SQL命令中的占位符，转换为?
        std::string preparedSql = command;
        size_t pos = 0;
        int paramCount = 0;
        
        // 统计参数数量并替换占位符为?
        while ((pos = preparedSql.find("%d", pos)) != std::string::npos) {
            preparedSql.replace(pos, 2, "?");
            paramCount++;
        }
        
        pos = 0;
        while ((pos = preparedSql.find("%s", pos)) != std::string::npos) {
            preparedSql.replace(pos, 2, "?");
            paramCount++;
        }
        
        pos = 0;
        while ((pos = preparedSql.find("%f", pos)) != std::string::npos) {
            preparedSql.replace(pos, 2, "?");
            paramCount++;
        }
        
        pos = 0;
        while ((pos = preparedSql.find("%l", pos)) != std::string::npos) {
            preparedSql.replace(pos, 2, "?");
            paramCount++;
        }
        
        // 检查参数数量是否匹配
        if (paramCount != sizeof...(args)) {
            throw std::invalid_argument("wrong args nums!");
        }
        
        try {
            // 创建预处理语句
            std::unique_ptr<sql::PreparedStatement> pstmt(con->prepareStatement(preparedSql));
            
            // 绑定参数
            int index = 1;
            bindParameters(pstmt.get(), index, args...);
            
            // 执行查询并返回结果集
            return pstmt->executeUpdate();
        } catch (const sql::SQLException& e) {
            throw std::runtime_error("fail to SQL update: " + std::string(e.what()));
        }  
    }

    /**
     * 初始化连接池，创建最小数量的连接
     */
    void init() {
        if (initialized_) return;
        
        std::lock_guard<std::mutex> lock(poolMutex_);
        
        try {
            // 创建初始连接
            for (size_t i = 0; i < minConnections_; ++i) {
                connections_.push(createConnection());
            }
            initialized_ = true;
        } catch (const std::exception& e) {
            std::cerr << "连接池初始化失败: " << e.what() << std::endl;
            throw;
        }
    }


    /**
     * 从连接池获取一个数据库连接
     * @return 数据库连接的智能指针，使用完后自动释放回池中
     */
    std::shared_ptr<sql::Connection> getConnection() {
        if (!initialized_) {
            throw std::runtime_error("get connetion without init");
        }
        
        std::unique_lock<std::mutex> lock(poolMutex_);
        
        // 如果可以，就创建新连接
        if (connections_.empty() && activeConnections_ < maxConnections_) {
            addNewConnection();
        }
        
        // 达到最大连接数，则等待
        if (connections_.empty()) {
            if (!cv_.wait_for(lock, std::chrono::milliseconds(timeout_),
                             [this] { return !connections_.empty(); })) {
                throw std::runtime_error("timeout when get connection with SQL");
            }
        }
        
        // 从队列中取出一个连接
        std::unique_ptr<sql::Connection> conn = std::move(connections_.front());
        connections_.pop();
        
        // 检查连接是否有效，无效则创建新连接
        if (!isConnectionValid(conn.get())) {
            try {
                conn = createConnection();
            } catch (const std::exception& e) {
                // 连接创建失败，将连接放回队列并抛出异常
                // connections_.push(std::move(conn));
                // cv_.notify_one();
                throw;
            }
        }
        
        ++activeConnections_;
        
        // 使用自定义删除器的shared_ptr，确保连接在使用后返回池中
        return std::shared_ptr<sql::Connection>(
            conn.release(), // 转移所有权
            [this](sql::Connection* conn) {
                this->releaseConnection(conn); // 销毁时回到队列
            }
        );
    }
    
    /**
     * 关闭连接池，释放所有连接资源
     */
    void shutdown() {
        std::lock_guard<std::mutex> lock(poolMutex_);
        
        // 清空连接队列
        while (!connections_.empty()) {
            connections_.pop();
        }
        
        activeConnections_ = 0;
        initialized_ = false;
    }
    
    /**
     * 获取当前活跃连接数
     */
    size_t getActiveConnections() const {
        return activeConnections_;
    }
    
    /**
     * 获取当前空闲连接数
     */
    size_t getIdleConnections() {
        std::lock_guard<std::mutex> lock(poolMutex_);
        return connections_.size();
    }

private:
    /**
     * 创建新的数据库连接
     */
    std::unique_ptr<sql::Connection> createConnection() {
        try {
            // 获取驱动
            sql::Driver* driver = get_driver_instance();
            std::unique_ptr<sql::Connection> conn(
                driver->connect(
                    "tcp://" + host_ + ":" + std::to_string(port_),
                    user_,
                    password_
                )
            );
            
            if (!conn || !conn->isValid()) {
                throw std::runtime_error("无法建立数据库连接");
            }
            
            if(database_!="") conn->setSchema(database_);
            conn->setAutoCommit(true);  // 默认自动提交
            
            return conn;
        } catch (const sql::SQLException& e) {
            std::cerr << "创建数据库连接失败: " << e.what() 
                      << " (错误码: " << e.getErrorCode() << ")" << std::endl;
            throw;
        }
    }
    
    /**
     * 添加新连接到连接池
     */
    void addNewConnection() {
        try {
            connections_.push(createConnection());
        } catch (const std::exception& e) {
            std::cerr << "添加新连接失败: " << e.what() << std::endl;
            throw;
        }
    }
    
    /**
     * 释放连接，将连接返回池中
     */
    void releaseConnection(sql::Connection* conn) {
        if (!conn) return;
        
        std::lock_guard<std::mutex> lock(poolMutex_);
        
        // 检查连接是否有效
        if (conn->isValid()) {
            connections_.push(std::unique_ptr<sql::Connection>(conn));
            --activeConnections_;
        } else {
            // 连接无效，关闭并丢弃
            try {
                conn->close();
            } catch (...) {}
            delete conn;
            
            // 创建一个新连接补充到池中
            if (connections_.size() < minConnections_) {
                addNewConnection();
            }
        }
        
        // 通知等待的线程有新的连接可用
        cv_.notify_one();
    }
    
    /**
     * 检查连接是否有效
     */
    bool isConnectionValid(sql::Connection* conn) {
        if (!conn || !conn->isValid()) {
            return false;
        }
        
        // 执行简单查询测试连接
        try {
            std::unique_ptr<sql::Statement> stmt(conn->createStatement());
            std::unique_ptr<sql::ResultSet> res(stmt->executeQuery("SELECT 1"));
            return res->next() && res->getInt(1) == 1;
        } catch (...) {
            return false;
        }
    }

    // 辅助函数：递归绑定参数
    template<typename T, typename ...Args>
    void bindParameters(sql::PreparedStatement* pstmt, int& index, T value, Args... args) {
        if constexpr (std::is_integral_v<T> && !std::is_same_v<T, bool>) {
            pstmt->setInt(index++, static_cast<int>(value));
        } else if constexpr (std::is_floating_point_v<T>) {
            pstmt->setDouble(index++, value);
        } else if constexpr (std::is_same_v<T, std::string> || std::is_same_v<T, const char*>) {
            pstmt->setString(index++, value);
        } else if constexpr (std::is_same_v<T, bool>) {
            pstmt->setBoolean(index++, value);
        } else if constexpr (std::is_same_v<T, long> || std::is_same_v<T, long long>) {
            pstmt->setInt64(index++, value);
        } else {
            // 不支持的类型
            throw std::invalid_argument("unknowen type of paramater: "+std::to_string(value));
        }
        
        // 递归绑定剩余参数
        if constexpr (sizeof...(args) > 0) {
            bindParameters(pstmt, index, args...);
        }
    }
};

#endif