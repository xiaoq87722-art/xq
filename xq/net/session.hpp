#ifndef __XQ_NET_SESSION_HPP__
#define __XQ_NET_SESSION_HPP__


#include "xq/net/net.in.h"
#include "xq/net/buffer.hpp"
#include "xq/utils/mpsc.hpp"
#include <netinet/in.h>
#include <format>


namespace xq {
namespace net {


class Listener;
class Reactor;


class Session {
    Session(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(const Session&) = delete;
    Session& operator=(Session&&) = delete;


public:
    explicit Session() noexcept : wque_(4, 16) {}


    ~Session() noexcept {
        release();
    }


    void
    init(SOCKET cfd, Listener* l, Reactor* r) noexcept;


    void
    release() noexcept;


    SOCKET
    fd() const {
        return cfd_;
    }


    const char*
    remote_addr() const {
        return remote_.c_str();
    }


    uint64_t
    active_time() const {
        return active_time_;
    }


    void
    active_time(uint64_t v) noexcept {
        active_time_ = v;
    }


    Buffer*
    current_wbuf() noexcept {
        return &cwbuf_;
    }


    std::string
    to_string() const {
        return std::format("[{}]{}", cfd_, remote_);
    }


    bool
    closed_by_server() const {
        return cbs_;
    }


    Listener*
    listener() noexcept {
        return listener_;
    }


    const Listener*
    listener() const {
        return listener_;
    }


    Reactor*
    reactor() noexcept {
        return reactor_;
    }


    const Reactor*
    reactor() const {
        return reactor_;
    }


    void
    sending(bool v) noexcept {
        sending_ = v;
    }


    /**
     * @brief 取消 session 会话. 
     *        该函数只能在 session 所属的 reactor 线程中调用.
     */
    void
    submit_cancel(bool auto_submit = false) noexcept;


    /**
     * @brief 提交 session 接收请求. 
     *        该函数只能在 session 所属的 reactor 线程中调用.
     */
    void
    submit_recv(bool auto_submit = false) noexcept;


    /**
     * @brief 发送消息, 如果 ctr(current thread reactor) 是 session的所属reactor 则直接发送消息. 
     *        否则通知 session 所在 reactor 发送消息
     * 
     * @param ctr: 当前线程的 reactor 
     */
    int
    send(Reactor* ctr, const uint8_t* data, size_t datalen, bool auto_submit = false) noexcept;


    /**
     * @brief 提交发送. 会将 session 发送缓冲区的数据发送出去. 
     *        该函数只能在 session 所属的 reactor 线程中调用.
     */
    void
    submit_send(bool auto_submit = false) noexcept;


    /** 
     * @brief 将 session 发送队列(wque_) 中的数据移交到 cwbuf_ 中.
     */
    void
    drain_wque() noexcept;


private:
    SOCKET cfd_ { INVALID_SOCKET };

    /** closed by server 是否由服务端主动关闭 */
    bool cbs_ { false };

    /** 最后活跃时间 */
    uint64_t active_time_ { 0 };

    /** 所属监听器 */
    Listener* listener_ { nullptr };

    /** 所属 reactor */
    Reactor* reactor_ { nullptr };

    /** 对端地址 */
    sockaddr_in addr_ {};

    /** 对端地址字符串 */
    std::string remote_;

    /** 当前发送缓冲区 current write buffer */
    Buffer cwbuf_;

    /** 当前接收缓冲区 current read buffer */
    Buffer crbuf_;

    /** 发送队列 */
    xq::utils::MPSC<Buffer*> wque_;

    /** 是否处理发送状态 */
    std::atomic_bool sending_ { false };
}; // class Session;


} // namespace net
} // namespace xq


#endif // __XQ_NET_SESSION_HPP__