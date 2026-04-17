#include "xq/net/net.in.hpp"
#include "xq/net/conf.hpp"
#include "xq/utils/memory.hpp"
#include <sys/mman.h>
#include <sys/socket.h>
#include <emmintrin.h>
#include <netdb.h>
#include <unistd.h>
#include <string>


SOCKET
xq::net::tcp_listen(const char* host, int rcv_buf, int snd_buf) noexcept {
    std::string host_str(host);
    if (host_str.empty()) {
        return INVALID_SOCKET;
    }

    auto pos = host_str.find_last_of(':');
    std::string port_str;
    if (pos != std::string::npos) {
        port_str = host_str.substr(pos + 1);
        host_str = host_str.substr(0, pos);
    }

    if (port_str.empty()) {
        return INVALID_SOCKET;
    }

    if (host_str.empty()) {
        host_str = "0.0.0.0";
    }

    struct addrinfo hints {};
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_flags = AI_PASSIVE;

    struct addrinfo* res = nullptr;
    struct addrinfo* rp = nullptr;

    int ret = ::getaddrinfo(host_str.c_str(), port_str.c_str(), &hints, &res);
    if (ret) {
        xERROR("getaddrinfo failed: [{}] {}", ret, ::gai_strerror(ret));
        return INVALID_SOCKET;
    }

    SOCKET lfd = INVALID_SOCKET;
    for (rp = res; rp; rp = rp->ai_next) {
        lfd = ::socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (lfd == INVALID_SOCKET) {
            continue;
        }

        if (set_nonblocking(lfd) < 0) {
            ::close(lfd);
            lfd = INVALID_SOCKET;
            continue;
        }

        int optval = 1;
        if (::setsockopt(lfd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval))) {
            ::close(lfd);
            lfd = INVALID_SOCKET;
            continue;
        }

        if (snd_buf >= 0 && ::setsockopt(lfd, SOL_SOCKET, SO_SNDBUF, &snd_buf, sizeof(snd_buf))) {
            ::close(lfd);
            lfd = INVALID_SOCKET;
            continue;
        }

        if (rcv_buf >= 0 && ::setsockopt(lfd, SOL_SOCKET, SO_RCVBUF, &rcv_buf, sizeof(rcv_buf))) {
            ::close(lfd);
            lfd = INVALID_SOCKET;
            continue;
        }

        if (!::bind(lfd, rp->ai_addr, rp->ai_addrlen)) {
            break;
        }

        ::close(lfd);
        lfd = INVALID_SOCKET;
    }

    ::freeaddrinfo(res);

    return lfd;
}