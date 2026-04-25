#ifndef __XQ_NET_SESSION_HPP__
#define __XQ_NET_SESSION_HPP__


#include "xq/net/conf.hpp"
#include "xq/net/event.hpp"
#include "xq/net/net.in.hpp"
#include "xq/utils/mpsc.hpp"
#include "xq/utils/ring_buf.hpp"


namespace xq::net {


class Reactor;
class Listener;


class Session {
    friend class Reactor;
    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;


public:
    static Session*
    create() {
        return new Session;
    }


    ~Session() noexcept
    {}


    void
    init(SOCKET fd, Listener* listener, Reactor* reactor) noexcept;


    void
    release() noexcept;


    bool
    valid() const noexcept {
        return valid_.load(std::memory_order_acquire);
    }


    SOCKET
    fd() noexcept {
        return fd_;
    }


    bool
    closed_by_server() const noexcept {
        return cbs_;
    }


    Listener*
    listener() noexcept {
        return listener_;
    }


    Reactor*
    reactor() noexcept {
        return reactor_;
    }


    EpollArg*
    arg() noexcept {
        return &ea_;
    }


    std::string
    to_string() const noexcept {
        return std::format("[{}] {}", fd_, sockaddr_to_string((sockaddr*)&addr_));
    }


    xq::utils::RingBuf&
    rbuf() noexcept {
        return rbuf_;
    }


    int
    recv() noexcept;


    /**
     * @brief 发送数据. 支持任意线程调用.
     *        - 同线程 (reactor 线程): 直接 drain + writev.
     *        - 跨线程: 由 active_senders_ + valid_ 保护, 无 UAR 风险.
     *        返回 >=0 已发送字节数; <0 表示错误或 session 已失效.
     */
    int
    send(const char* data, size_t len) noexcept;


    int
    broadcast(const char* data, size_t len) noexcept;


private:
    Session() noexcept
    {}


    bool
    is_timeout(time_t now) const noexcept {
        return now - last_active_ >= Conf::instance()->timeout();
    }


    bool cbs_ { false };
    SOCKET fd_ { INVALID_SOCKET };
    time_t last_active_ { 0 };
    Listener* listener_ { nullptr };
    Reactor* reactor_ { nullptr };
    EpollArg ea_ {};
    sockaddr_storage addr_ {};

    bool wait_out_ { false };
    std::atomic<bool> sending_ { false };

    /**
     * @brief Session 对象是否有效.
     *        init() 末尾 store(true) 发布, release() 开头 CAS true->false 关门.
     *        所有跨线程 send 必须在 active_senders_ 保护下再次校验此 flag.
     */
    std::atomic<bool> valid_ { false };

    /**
     * @brief 跨线程 send 的 in-flight 计数.
     *        writer 进入前 ++, 退出前 --. release 在 CAS valid_ 后自旋等归 0 再独占清理.
     *        配合 valid_ 解决池复用 UAR race.
     */
    std::atomic<uint32_t> active_senders_ { 0 };

    /**
     * @brief 发送缓冲区.
     *        容量上限为性能优化的硬边界, 正常路径不应打满.
     *        若 sbuf_.write 触发 ASSERT, 说明业务写入速率 > kernel 消费速率且积压超过 WBUF_MAX, 需调大 WBUF_MAX.
     */
    xq::utils::RingBuf sbuf_ { WBUF_MAX };

    /**
     * @brief 接收缓冲区.
     *        容量上限为性能优化的硬边界; 若 on_data 长期不消费, recv 会返回 0 而不是崩溃.
     *        不足时调大 RBUF_MAX.
     */
    xq::utils::RingBuf rbuf_ { RBUF_MAX };

    /**
     * @brief 跨线程发送队列 (MPSC). 跨线程 send 走此通路, 同线程 send 绕过.
     *        若 enqueue ASSERT 触发, 说明跨线程峰值突发超过容量, 需调大 per_shard_size.
     */
    xq::utils::MPSC<xq::utils::SendBuf> sque_ { 4, 64 };
}; // class Session;

    
} // namespace xq::net


#endif // __XQ_NET_SESSION_HPP__