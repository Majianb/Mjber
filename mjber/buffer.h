#pragma once
#include <vector>
#include <cstring>
#include <atomic>
#include "rw_mutex.h"




/*
    环形缓冲区
*/
class Buffer {
public:
    static const size_t DEFAULT_SIZE = 4096; // 4KB 初始容量
    explicit Buffer(size_t initial_size = DEFAULT_SIZE)
        : buffer_(initial_size), read_pos_(0), write_pos_(0) {}

    // 基础读写接口
    size_t read(void* buffer, size_t len);
    size_t write(const void* buffer, size_t len);
    std::pair<size_t,size_t> commitRead(size_t readlen);
    // 固定长度读写（协程友好）
    size_t readFixSize(void* buffer, size_t len);
    size_t writeFixSize(const void* buffer, size_t len);

    // 零拷贝支持
    // void getWriteBuffers(std::vector<iovec>& iovs, size_t len);
    void commitWrite(size_t len);

    // 容量管理
    std::tuple<size_t,size_t> readableBytes(size_t len);
    size_t writableBytes() const;
    size_t readableBytes() const;
    void ensureWritable(size_t len);

private:
    void expandCapacity(size_t len); // 动态扩容

    std::vector<uint8_t> buffer_;
    size_t read_pos_ = 0;
    size_t write_pos_ = 0;
    mutable RWMutex mutex_;
    mutable std::mutex uniquem_;
};

//返回读到的数据长度
size_t Buffer::read(void* buffer, size_t len) {
    RWMutex::ReadLockGuard rlock(mutex_);
    if (len == 0) return 0;

    std::pair<size_t,size_t> r = commitRead(len); //更新指针

    size_t pos = r.first;
    size_t readable = r.second;

    if (readable == 0) return -1; // 无数据

    size_t first_chunk = std::min(readable, buffer_.size() - pos);
    
    memcpy(buffer, &buffer_[pos], first_chunk);
    if (readable > first_chunk) {
        memcpy(static_cast<char*>(buffer) + first_chunk, &buffer_[0], readable - first_chunk);
    }
    return readable; 
}

//总是可以写入
size_t Buffer::write(const void* data, size_t len) {
    ensureWritable(len);
    {
        RWMutex::WriteLockGuard wgard(mutex_);
    

        size_t first_chunk = std::min(len, buffer_.size() - write_pos_);
        memcpy(&buffer_[write_pos_], data, first_chunk);
        
        if (len > first_chunk) {
            memcpy(&buffer_[0], static_cast<const char*>(data) + first_chunk, 
                len - first_chunk);
        }
        
        write_pos_ = (write_pos_ + len) % buffer_.size();
    }
    return len;
}

// 零拷贝优化接口

// void Buffer::getWriteBuffers(std::vector<iovec>& iovs, size_t len) {
//     iovs.clear();
//     size_t writable = writableBytes();
//     len = std::min(len, writable);

//     if (write_pos_ + len <= buffer_.size()) {
//         iovs.push_back({&buffer_[write_pos_], len});
//     } else {
//         size_t first = buffer_.size() - write_pos_;
//         iovs.push_back({&buffer_[write_pos_], first});
//         iovs.push_back({&buffer_[0], len - first});
//     }
// }

void Buffer::commitWrite(size_t len) {
    write_pos_ = (write_pos_ + len) % buffer_.size();
}

// 动态扩容策略,确保可写
void Buffer::ensureWritable(size_t len) {
    if (writableBytes() >= len) return;
    {
        RWMutex::WriteLockGuard wguard(mutex_);
        size_t new_capacity = buffer_.size();
        while (new_capacity - readableBytes() - 1 < len) {
            new_capacity *= 2; // 指数扩容策略
        }

        std::vector<uint8_t> new_buf(new_capacity);
        
        // 迁移数据到新缓冲区
        size_t readable = readableBytes();
        readFixSize(&new_buf[0], readable); // 复用固定读取
        
        buffer_.swap(new_buf);
        read_pos_ = 0;
        write_pos_ = readable;
    }
}

// 确保读取固定长度数据，无数据时等待
// size_t Buffer::readFixSize(void* buffer, size_t len) {
//     size_t total = 0;
//     while (total < len) {
//         size_t n = read(static_cast<char*>(buffer) + total, len - total);
//         if (n <= 0) {
//             if (errno == EAGAIN) {
//                 Fiber::YieldToHold(); 
//                 continue;
//             }
//             return n;
//         }
//         total += n;
//     }
//     return total;
// }

//返回可读的位置和长度
std::pair<size_t,size_t> Buffer::commitRead(size_t len){
    RWMutex::LockGuard lockguard(mutex_);
    size_t readable;
    size_t pos = read_pos_;
    if (write_pos_ >= read_pos_) {
        readable = write_pos_ - read_pos_;
        readable = std::min(readable,len);
        read_pos_ += readable; //更新读位置
        return std::pair<size_t,size_t>(pos,readable);
    }
    else{
        readable = buffer_.size() - read_pos_ + write_pos_;
        readable = std::min(readable,len);
        read_pos_ = (read_pos_ + readable)%buffer_.size();//更新读位置
        return std::pair<size_t,size_t>(pos,readable);
    }
}

// 获取当前缓冲区中可写的字节数
size_t Buffer::writableBytes() const {
    RWMutex::LockGuard lockguard(mutex_);
    if (write_pos_ >= read_pos_) {
        return buffer_.size() - write_pos_ + read_pos_ - 1;
    }
    return read_pos_ - write_pos_ - 1;
}
// 获取当前缓冲区中可读的字节数
size_t Buffer::readableBytes() const {
    RWMutex::LockGuard lockguard(mutex_);
    if (write_pos_ >= read_pos_) {
        return write_pos_ - read_pos_;
    }
    return buffer_.size() - read_pos_ + write_pos_;
}


// mutable 允许被const修饰的函数改变
// RAII 是一种思想，将资源与类绑定，防止内存泄漏
// RAII实现的锁也叫守卫
// volatile 声明的变量不会使用寄存器优化，每次必须从内存读