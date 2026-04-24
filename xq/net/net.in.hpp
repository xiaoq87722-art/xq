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


/** 
 * @brief 读缓冲区大小, 用于 recv 操作时的 RingBuf 大小
 */
constexpr int RBUF_MAX = 1024 * 128;


/**
 * @brief 写缓冲区大小, 用于 send 操作时的 RingBuf 大小
 */
constexpr int WBUF_MAX = 1024 * 128;


// ---------------------- 状态 ----------------------
constexpr int STATE_STOPPED = 0;  // 停止状态
constexpr int STATE_STARTING = 1; // 正在开启
constexpr int STATE_RUNNING = 2;  // 运行状态
constexpr int STATE_STOPPING = 3; // 正在停止


namespace xq::net {


/**
 * @brief 用于 epoll_event.data.ptr
 */
struct EpollArg {
    enum class Type {
        Event = 1,    // 来自 event fd 的通知
        Listener = 2, // 来自 Listen fd 的通知
        Session = 3,  // 来自 Session fd 的通知
        Conn = 4,     // 来自 Conn fd 的通知
    };

    /** 参数类型 */
    Type type;

    /** 参数数据 */
    void* data;
};


/**
 * @brief 获取 TCP 监听套接字
 * 
 * @return 成功返回 listen fd, 否则返回 INVALID_SOCKET
 */
SOCKET
tcp_listen(const char* endpoint) noexcept;


/**
 * @brief 获取 TCP 连接套接字
 * 
 * @return 成功返回 client fd, 否则返回 INVALID_SOCKET
 */
SOCKET
tcp_connect(const char* host) noexcept;


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


/**
 * @brief 初始化 epoll fd & event fd
 */
void
init_epoll_event(SOCKET* epfd, SOCKET* evfd, EpollArg* ea) noexcept;


/**
 * @brief 释放 epoll fd & event fd
 */
void
release_epoll_event(SOCKET* epfd, SOCKET* evfd) noexcept;


} // namespace xq::net


#endif // __XQ_NET_IN_HPP__