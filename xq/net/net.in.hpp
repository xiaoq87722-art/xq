#ifndef __XQ_NET_IN_HPP__
#define __XQ_NET_IN_HPP__


#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/socket.h>


#include "xq/utils/log.hpp"
#include "xq/utils/memory.hpp"


typedef int SOCKET;
#define INVALID_SOCKET (-1)


constexpr int RBUF_MAX = 1024 * 64;
constexpr int WBUF_MAX = 1024 * 128;


constexpr int STATE_STOPPED = 0;  // 停止状态
constexpr int STATE_STARTING = 1; // 正在开启
constexpr int STATE_RUNNING = 2;  // 运行状态
constexpr int STATE_STOPPING = 3; // 正在停止


namespace xq::net {


/**
 * @brief 获取 TCP 监听套接字
 * 
 * @return 成功返回 listen fd, 否则返回 INVALID_SOCKET
 */
SOCKET
tcp_listen(const char* endpoint, int rcv_buf, int snd_buf) noexcept;


/**
 * @brief 设置 fd 为非阻塞
 * 
 * @return 成功返回 0, 否则返回 -1
 */
inline int
set_nonblocking(int fd) noexcept {
    int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags == -1) {
        return -1;
    }

    if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK)) {
        return -1;
    }

    return 0;
}


/**
 * @brief 获取下一个可用的文件描述符
 */
inline int
get_next_fd() noexcept {
    int fd = ::fcntl(STDERR_FILENO + 1, F_DUPFD, 0); 
    ASSERT(fd > 0, "::fcntl(STDERR_FILENO + 1, F_DUPFD, 0) failed: [{}] {}", errno, ::strerror(errno));
    ::close(fd);
    return fd;
}


/** 
 * @brief sockaddr 转为 string 表达式
 */
inline std::string
sockaddr_to_string(const sockaddr* addr) noexcept {
    char ip[INET6_ADDRSTRLEN] = {0};
    uint16_t port = 0;

    if (addr->sa_family == AF_INET) {
        auto* v4 = (const sockaddr_in*)addr;
        ::inet_ntop(AF_INET, &v4->sin_addr, ip, sizeof(ip));
        port = ::ntohs(v4->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        auto* v6 = (const sockaddr_in6*)addr;
        ::inet_ntop(AF_INET6, &v6->sin6_addr, ip, sizeof(ip));
        port = ::ntohs(v6->sin6_port);
    } else {
        return "";
    }

    return std::string(ip) + ":" + std::to_string(port);
}


} // namespace xq::net


#endif // __XQ_NET_IN_HPP__