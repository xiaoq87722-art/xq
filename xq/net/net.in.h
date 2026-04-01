#ifndef __XQ_NET_IN_HPP__
#define __XQ_NET_IN_HPP__


#include "xq/utils/log.hpp"
#include <liburing.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include "xq/utils/memory.hpp"


typedef int SOCKET;
#define INVALID_SOCKET (-1)


namespace xq {
namespace net {


constexpr int STATE_STOPPED = 0;  // 停止状态
constexpr int STATE_STARTING = 1; // 正在开启
constexpr int STATE_RUNNING = 2;  // 运行状态
constexpr int STATE_STOPPING = 3; // 正在停止


enum class RingCommand {
    NONE,     // 无
    R_ACCEPT, // session new connection
    R_STOP,   // reactor stop
    R_TIMER,  // reactor timer,
    R_SEND,   // reactor send
    S_RECV,   // session read
    S_SEND,   // session write
    S_CANCEL, // session Cancel
    C_CONN,   // conn connection
    C_TIMER,  // conn timer
    C_RECV,
};


/**
 * @brief io_uring 传递事件
 *   该结构体很小, 所以没必要用到lockfree-threadsafe 的对象池, 直接使用 mi_malloc 性能更好
 */
struct RingEvent {
    static RingEvent*
    create() noexcept {
        auto p = xq::utils::malloc(sizeof(RingEvent));
        return new(p) RingEvent;
    }


    static void
    destroy(RingEvent* p) noexcept {
        if (p) {
            p->~RingEvent();
            xq::utils::free(p);
        }
    }

    ~RingEvent() noexcept {}

    RingEvent(const RingEvent&) = delete;
    RingEvent(RingEvent&&) = delete;
    RingEvent& operator=(const RingEvent&) = delete;
    RingEvent& operator=(RingEvent&&) = delete;


    void
    init(RingCommand c, SOCKET f = INVALID_SOCKET, void* d = nullptr, uint64_t g = 0) noexcept {
        cmd = c;
        fd = f;
        ex = d;
        gen = g;
    }


    /** 命令 */
    RingCommand cmd { RingCommand::NONE };

    /** socket fd */
    SOCKET fd { INVALID_SOCKET };

    /** 扩展数据 */
    void* ex { nullptr };

    /** 世代号 */
    uint64_t gen { 0 };

private:
    RingEvent() noexcept {}
};


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
 * @brief 获取io_uring_sqe
 * 
 * @param uring io_uring实例
 * 
 */
inline io_uring_sqe*
acquire_sqe(io_uring* uring) noexcept {
    auto *sqe = io_uring_get_sqe(uring);
    if (!sqe) {
        int ret = ::io_uring_submit(uring);
        ASSERT(ret >= 0, "io_uring_submit failed: {}, {}", -ret, ::strerror(-ret));
        sqe = ::io_uring_get_sqe(uring);
    }

    ASSERT(sqe != nullptr, "获取 io_uring_sqe 失败, 请将 que_depth 设置为更大值");
    return sqe;
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
        port = ntohs(v4->sin_port);
    } else if (addr->sa_family == AF_INET6) {
        auto* v6 = (const sockaddr_in6*)addr;
        ::inet_ntop(AF_INET6, &v6->sin6_addr, ip, sizeof(ip));
        port = ntohs(v6->sin6_port);
    } else {
        return "";
    }

    return std::string(ip) + ":" + std::to_string(port);
}


io_uring_buf_ring*
init_io_uring_with_br(io_uring* uring, std::vector<uint8_t*>& brbufs) noexcept;


void
release_io_uring_with_br(io_uring* uring, io_uring_buf_ring* ptr, std::vector<uint8_t*>& brbufs) noexcept;


void
recycle_buf_ring(io_uring_buf_ring* br, uint8_t* buf, uint16_t bid) noexcept;


} // namespace net
} // namespace xq


#endif // __XQ_NET_IN_HPP__