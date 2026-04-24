#ifndef __XQ_NET_REACTOR_HPP__
#define __XQ_NET_REACTOR_HPP__


#include <atomic>
#include <unordered_map>


#include "xq/net/event.hpp"
#include "xq/net/session.hpp"
#include "xq/utils/mpsc.hpp"


namespace xq::net {


/**
 * @brief 反应器
 */
class Reactor {
    Reactor(const Reactor&) = delete;
    Reactor& operator=(const Reactor&) = delete;
    Reactor(Reactor&&) = delete;
    Reactor& operator=(Reactor&&) = delete;


public:
    /**
     * @brief 构造函数
     */
    explicit Reactor() noexcept
    {}


    /**
     * @brief 析构函数
     */
    ~Reactor() noexcept
    {}


    /**
     * @brief 是否运行
     */
    bool
    running() const noexcept {
        return state_.load() == STATE_RUNNING;
    }


    /**
     * @brief epoll fd
     */
    SOCKET
    epfd() const noexcept {
        return epfd_;
    }


    /**
     * @brief 当前时间, 并非 unix timestamp
     */
    time_t
    tnow() const noexcept {
        return tnow_;
    }


    /**
     * @brief thread id
     */
    std::thread::id
    tid() const noexcept {
        return t_.get_id();
    }

    
    /**
     * @brief 启动
     */
    void
    run() noexcept {
        int state_stopped = STATE_STOPPED;

        if (state_.compare_exchange_strong(state_stopped, STATE_STARTING)) {
            t_ = std::thread(&Reactor::start, this);
        }
    }

    
    /**
     * @brief 停止
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
     * @brief 等待结束
     */
    void
    join() noexcept {
        if (t_.joinable()) {
            t_.join();
        }
    }


    /**
     * @brief 投递消息
     */
    void
    post(Event ev) noexcept;


    /**
     * @brief 添加会话
     */
    void
    add_session(SOCKET fd, Session* s) noexcept {
        sessions_[fd] = s;
    }


    /**
     * @brief 移除会话
     */
    void
    remove_session(SOCKET fd) noexcept {
        auto itr = sessions_.find(fd);
        if (itr != sessions_.end()) {
            auto s = itr->second;
            s->listener()->service()->on_disconnected(s);
            ASSERT(!::epoll_ctl(epfd_, EPOLL_CTL_DEL, s->fd(), nullptr), "epoll_ctl failed");
            sessions_.erase(itr);
            s->release();
        }
    }


private:
    /**
     * @brief 启动
     */
    void
    start() noexcept;


    /**
     * @brief 事件句柄, 处理的事件有: 
     * 
     *         1, Event::Type::Accept 连接事件
     *
     *         2, Event::Type::Send Session 发送事件
     *
     *         3, Event::Type::Broadcast Session 广播事件
     */
    void
    event_handle(EpollArg* ea) noexcept;


    /**
     * @brief 会话读句柄
     */
    void
    session_recv_handle(EpollArg* ea) noexcept;


    /**
     * @brief 会话写句柄
     */
    void
    session_send_handle(EpollArg* ea) noexcept;


    /**
     * @brief 停止句柄
     */
    void
    on_stopped() noexcept;


    /**
     * @brief Event::Type::Accept 句柄
     */
    void
    on_accept(void* params) noexcept;


    /**
     * @brief Event::Type::Send 句柄
     */
    void
    on_send(void* params) noexcept;


    /**
     * @brief Event::Type::Broadcast 句柄
     */
    void
    on_broadcast(void* params) noexcept;


    /**
     * @brief 定时器句柄
     */
    void
    timer_handle() noexcept;


    /** epoll fd */
    SOCKET epfd_ { INVALID_SOCKET };

    /** event fd */
    SOCKET evfd_ { INVALID_SOCKET };

    /** 当前时间 */
    time_t tnow_ { 0 };

    /** 状态 */
    std::atomic<int> state_ { STATE_STOPPED };

    /** 是否正在处理事件 */
    std::atomic<bool> processing_ { false };

    /** 当前线程 */
    std::thread t_ {};

    /**
     * @brief 事件队列 (MPSC), 承接 Accept / Send / Broadcast 事件.
     *        若 enqueue ASSERT 触发, 说明峰值突发超过容量, 需调大 per_shard_size.
     */
    xq::utils::MPSC<Event> evque_ { 8, 4096 };

    /** 会话池 */
    std::unordered_map<SOCKET, Session*> sessions_;
}; // class Reactor;


} // namespace xq::net


#endif // __XQ_NET_REACTOR_HPP__