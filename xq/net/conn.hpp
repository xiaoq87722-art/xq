#ifndef __XQ_NET_CONN_HPP__
#define __XQ_NET_CONN_HPP__


#include "xq/net/net.in.h"


namespace xq {
namespace net {


class Connector;


class Conn {


public:
    explicit Conn(Connector* ctor) noexcept;


    ~Conn() {
        if (cfd_ != INVALID_SOCKET) {
            ::close(cfd_);
        }
    }


    const char* host() const {
        return host_.c_str();
    }


    SOCKET fd() const {
        return cfd_;
    }


    void submit_connect(const std::string& host) noexcept;


    void submit_recv() noexcept;


    Connector* connector() {
        return connector_;
    }


private:
    /** 所属 Connector 对象 */
    Connector* connector_ { nullptr };

    /** 连接套接字 */
    SOCKET cfd_ { INVALID_SOCKET };

    /** 远端地址 */
    sockaddr_in raddr_ {};

    /** 本端地址 */
    sockaddr_in caddr_ {};

    /** 服务端地址 */
    std::string host_;
}; // class Conn;

    
} // namespace net
} // namespace xq


#endif // __XQ_NET_CONN_HPP__