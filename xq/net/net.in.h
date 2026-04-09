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
    create(RingCommand c, SOCKET f = INVALID_SOCKET, void* d = nullptr, uint64_t g = 0) noexcept {
        auto p = xq::utils::malloc(sizeof(RingEvent));
        return new(p) RingEvent(c, f, d, g);
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


    /** 命令 */
    RingCommand cmd { RingCommand::NONE };

    /** socket fd */
    SOCKET fd { INVALID_SOCKET };

    /** 扩展数据 */
    void* ex { nullptr };

    /** 世代号 */
    uint64_t gen { 0 };

    /**
     * multishot recv teardown 标记:
     * on_data 返回非零且 F_MORE 为真时 session 已被 remove，
     * 但 ev 不能立即释放，需等内核的最终 cancel CQE (F_MORE 清零)。
     * 设此标志并增加 pending_recv_teardowns_ 以阻止 reactor 提前退出。
     */
    bool teardown { false };

private:
    RingEvent(RingCommand c, SOCKET f, void* d, uint64_t g) noexcept
        : cmd(c), fd(f), ex(d), gen(g)
    {}
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


} // namespace net
} // namespace xq


#endif // __XQ_NET_IN_HPP__