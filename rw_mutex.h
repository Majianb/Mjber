#include <mutex>
#include <condition_variable>


/*
    读写锁实现：
    读锁：
        进入：没有写进程时进入，读者数加一
        退出：读者数减1，如果无读者则唤醒写者
    写锁：
        进入：没有读进程且没有写者进入，设置在写
        退出：不写，如果有写者则唤醒。

    将类与锁绑定作为守卫，禁止拷贝和复制。

*/
class RWMutex {
public:
    // 读锁守卫（RAII）
    class ReadLockGuard {
    public:
        explicit ReadLockGuard(RWMutex& mtx) : mtx_(mtx) { mtx_.ReadLock(); }
        ~ReadLockGuard() { mtx_.ReadUnlock(); }
        ReadLockGuard(const ReadLockGuard&) = delete;
        ReadLockGuard& operator=(const ReadLockGuard&) = delete;
    private:
        RWMutex& mtx_;
    };

    // 写锁守卫（RAII）
    class WriteLockGuard {
    public:
        explicit WriteLockGuard(RWMutex& mtx) : mtx_(mtx) { mtx_.WriteLock(); }
        ~WriteLockGuard() { mtx_.WriteUnlock(); }
        WriteLockGuard(const WriteLockGuard&) = delete;
        WriteLockGuard& operator=(const WriteLockGuard&) = delete;
    private:
        RWMutex& mtx_;
    };
    // 临界区锁守卫
    class LockGuard {
        public:
            explicit LockGuard(RWMutex& mtx) : mtx_(mtx) { mtx_.Lock(); }
            ~LockGuard() { mtx_.Unlock(); }
            LockGuard(const LockGuard&) = delete;
            LockGuard& operator=(const LockGuard&) = delete;
        private:
            RWMutex& mtx_;
    };
    void ReadLock() {
        std::unique_lock<std::mutex> lock(mtx_);
        // 等待写线程完成且无写线程等待
        cond_read_.wait(lock, [this] { return write_cnt_ == 0 && !in_critical_section_; });
        ++read_cnt_;
    }

    void ReadUnlock() {
        std::unique_lock<std::mutex> lock(mtx_);
        if (--read_cnt_ == 0 && write_cnt_ > 0) {
            cond_write_.notify_one(); // 唤醒等待的写线程
        }
    }

    void WriteLock() {
        std::unique_lock<std::mutex> lock(mtx_);
        ++write_cnt_;
        // 等待所有读线程释放锁且无写线程正在执行
        cond_write_.wait(lock, [this] { return read_cnt_ == 0 && !in_write_; });
        in_write_ = true;
    }

    void WriteUnlock() {
        std::unique_lock<std::mutex> lock(mtx_);
        in_write_ = false;
        --write_cnt_;
        if (write_cnt_ == 0) {
            cond_read_.notify_all(); // 唤醒所有读线程
        } else {
            cond_write_.notify_one(); // 唤醒下一个写线程
        }
    }
    //临界区访问锁
    void Lock(){
        std::unique_lock<std::mutex> lock(mtx_);
        cond_unique_.wait(lock, [this] { return read_cnt_ == 0 && write_cnt_ == 0; });
        in_critical_section_ = true;
    }
    void Unlock(){
        std::unique_lock<std::mutex> lock(mtx_);
        in_critical_section_ = false;
        // 唤醒所有等待的读线程和写线程
        cond_unique_.notify_one();
        cond_write_.notify_one();
        cond_read_.notify_all();
    }
private:
    std::mutex mtx_;
    std::condition_variable cond_read_, cond_write_, cond_unique_;
    volatile size_t read_cnt_ = 0;
    volatile size_t write_cnt_ = 0;
    volatile bool in_write_ = false;
    volatile bool in_critical_section_ = false;
};