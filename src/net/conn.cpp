#include "xq/net/conn.hpp"
#include "xq/net/conf.hpp"
#include "xq/net/connector.hpp"
#include <netinet/tcp.h>
#include <netdb.h>
#include <arpa/inet.h>


xq::net::Conn::Conn(Connector* ctor) noexcept 
    : connector_(ctor) {
    SOCKET fd = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (fd < 0) {
        xFATAL("socket failed: [{}]{}", errno, ::strerror(errno));
    }

    if (set_nonblocking(fd)) {
        xFATAL("set_nonblocking failed: [{}]{}", errno, ::strerror(errno));
    }

    constexpr int nodelay_on = 1;
    if (::setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &nodelay_on, sizeof(nodelay_on))) {
        xFATAL("setsockopt TCP_NODELAY failed: [{}]{}", errno, ::strerror(errno));
    }

    auto rs = Conf::instance()->rcv_buf();
    if (::setsockopt(fd, SOL_SOCKET, SO_RCVBUF, &rs, sizeof(rs))) {
        xFATAL("setsockopt SO_RCVBUF failed: [{}]{}", errno, ::strerror(errno));
    }

    auto ss = Conf::instance()->snd_buf();
    if (::setsockopt(fd, SOL_SOCKET, SO_SNDBUF, &ss, sizeof(ss))) {
        xFATAL("setsockopt SO_SNDBUF failed: [{}]{}", errno, ::strerror(errno));
    }

    cfd_ = fd;
}


void
xq::net::Conn::submit_connect(const std::string& host) noexcept {
    auto pos = host.find_last_of(':');
    if (pos == std::string::npos) {
        xFATAL("无效的服务端地址: {}", host);
    }

    std::string port = host.substr(pos + 1);
    std::string ip = host.substr(0, pos);

    if (port.empty() || ip.empty()) {
        xFATAL("无效的服务端地址: {}", host);
    }

    host_ = host;

    struct addrinfo hints{};
    struct addrinfo* result;
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;

    if (::getaddrinfo(ip.c_str(), port.c_str(), &hints, &result) != 0) {
        xFATAL("getaddrinfo {} failed: [{}]{}", host, errno, ::strerror(errno));
    }

    auto* sqe = acquire_sqe(connector_->uring());
    auto* ev = RingEvent::create();
    ev->init(RingCommand::C_CONN, cfd_, this);
 
    ::memcpy(&raddr_, result->ai_addr, result->ai_addrlen);

    ::io_uring_prep_connect(sqe, cfd_, (sockaddr*)&raddr_, result->ai_addrlen);
    ::io_uring_sqe_set_data(sqe, ev);

    ::freeaddrinfo(result);
}


void
xq::net::Conn::submit_recv() noexcept {
    auto* sqe = acquire_sqe(connector_->uring());
    auto* ev = RingEvent::create();
    ev->init(xq::net::RingCommand::C_RECV, cfd_, this);
    ::io_uring_sqe_set_data(sqe, ev);
    ::io_uring_prep_recv_multishot(sqe, cfd_, nullptr, 0, 0);
    sqe->flags |= IOSQE_BUFFER_SELECT;
    sqe->buf_group = 1;
}