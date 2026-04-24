#ifndef __XQ_NET_CONN_WORKER_HPP__
#define __XQ_NET_CONN_WORKER_HPP__


#include <atomic>


#include "xq/net/conn.hpp"
#include "xq/net/net.in.hpp"
#include "xq/utils/spsc.hpp"


namespace xq::net {


/**
 * @brief 业务端处理器
 */
class Processor {
    Processor(const Processor&) = delete;
    Processor& operator=(const Processor&) = delete;
    Processor(Processor&&) = delete;
    Processor& operator=(Processor&&) = delete;


    /**
     * @brief 业务消息
     */
    struct Message {
        Conn* conn;
        void* data;
        int len;
    };


public:
    /**
     * @brief 构造函数
     */
    explicit Processor(Connector* c) noexcept
        : connector_(c)
    {}


    /**
     * @brief 析构函数
     */
    ~Processor() noexcept
    {}


    /**
     * @brief 是否正在运行
     */
    bool
    running() const noexcept {
        return state_.load() == STATE_RUNNING;
    }


    /**
     * @brief 是否正在处理业务
     */
    bool
    processing() const noexcept {
        return processing_.load();
    }


    /**
     * @brief 启动 processor
     */
    void
    run() noexcept {
        int state_stopped = STATE_STOPPED;
        if (state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
            t_ = std::thread(&Processor::start, this);
        }
    }


    /**
     * @brief 停止 processor
     */
    void
    stop() noexcept {
        int state_running = STATE_RUNNING;
        if (state_.compare_exchange_strong(state_running, STATE_STOPPING)) {
            constexpr uint64_t stop = 1;
            ASSERT(::write(evfd_, &stop, sizeof(stop)) == sizeof(stop), "write failed: [{}] {}", errno, ::strerror(errno));
        }
    }


    /**
     * @brief 等待 processor 结束
     */
    void
    join() noexcept {
        if (t_.joinable()) {
            t_.join();
        }
    }


    /**
     * @brief 投递 message
     */
    void
    post(Message e) noexcept {
        if (!running()) {
            return;
        }

        ASSERT(mque_.enqueue(std::move(e)), "队列已满");

        bool expected = false;
        if (processing_.compare_exchange_strong(expected, true)) {
            constexpr uint64_t event = 1;
            ASSERT(::write(evfd_, &event, sizeof(event)) == sizeof(event), "write failed: [{}] {}", errno, ::strerror(errno));
        }
    }


private:
    /**
     * @brief 启动 processor
     */
    void
    start() noexcept;


    /**
     * @brief 消息句柄
     */
    void
    msg_handle() noexcept;


    /** epoll fd */
    SOCKET epfd_ { INVALID_SOCKET };

    /** event fd, 用于 message queue */
    SOCKET evfd_ { INVALID_SOCKET };

    /** 所属 connector */
    Connector* connector_ { nullptr };

    /** 状态 */
    std::atomic<int> state_ { STATE_STOPPED };

    /** 是否正在处理业务, 减少 event fd 的唤醒 */
    std::atomic<bool> processing_ { false };

    /** 当前线程 */
    std::thread t_;

    /** 消息队列 */
    xq::utils::SPSC<Message> mque_ {};
}; // class Worker;


} // namespace xq::net


#endif // __XQ_NET_CONN_WORKER_HPP__