#ifndef __XQ_NET_CONN_RECVER_HPP__
#define __XQ_NET_CONN_RECVER_HPP__


#include "xq/net/event.hpp"
#include "xq/net/processor.hpp"
#include "xq/net/sender.hpp"
#include "xq/utils/spsc.hpp"


namespace xq::net {


/**
 * @brief 业务端的连接线程
 *        负责读业务
 */
class Connector {
    Connector(const Connector&) = delete;
    Connector& operator=(const Connector&) = delete;
    Connector(Connector&&) = delete;
    Connector& operator=(Connector&&) = delete;


public:
    static constexpr int MAX_CONN = 1024;


    /** 构造函数 */
    explicit Connector(IConnEvent* s) noexcept
        : service_(s)
    {}

    /** 析构函数 */
    ~Connector() noexcept {}


    /** 是否处于运行中 */
    bool
    running() const noexcept {
        return state_.load() == STATE_RUNNING;
    }


    /** 获取 sender (发送线程) 对象 */
    Sender*
    sender() noexcept {
        return &sender_;
    }


    /** 事件对象 */
    IConnEvent*
    service() noexcept {
        return service_;
    }


    /** 运行 */
    void
    run() noexcept;


    /** 停止 */
    void
    stop() noexcept {
        int expected = STATE_RUNNING;
        if (state_.compare_exchange_strong(expected, STATE_STOPPING)) {
            constexpr uint64_t stop = 1;
            ASSERT(::write(evfd_, &stop, sizeof(stop)) == sizeof(stop), "write failed: [{}] {}", errno, ::strerror(errno));
        }
    }
    

private:
    /** 连接端句柄 */
    void
    conn_handle(EpollArg* ea) noexcept;


    /** 事件句柄 */
    void
    event_handle() noexcept;


    /** 添加连接端 */
    void
    add_conn(Conn* conn) noexcept;


    /** 移除连接端 */
    void
    remove_conn(SOCKET fd) noexcept;


    /** epoll fd */
    SOCKET epfd_ { INVALID_SOCKET };

    /** event fd */
    SOCKET evfd_ { INVALID_SOCKET };

    /** 当前时间(s), 非 unix 时间戳 */
    time_t tnow_ { 0 };

    /** 连接事件句柄 */
    IConnEvent* service_ { nullptr };

    /** 状态 */
    std::atomic<int> state_ { STATE_STOPPED };

    /** 发送线程 */
    Sender sender_;

    /** 业务池 */
    std::vector<Processor*> procs_ {};

    /** 连接池 */
    Conn* conns_[MAX_CONN] { nullptr };
}; // class Connector;


} // namespace xq::net


#endif // __XQ_NET_CONN_RECVER_HPP__