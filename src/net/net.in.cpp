#include "xq/net/net.in.h"
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
        return -errno;
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

        static int optval = 1;
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


io_uring_buf_ring*
xq::net::init_io_uring_with_br(io_uring* uring, std::vector<uint8_t*> &brbufs) noexcept {
    const auto que_depth = xq::net::Conf::instance()->que_depth();

    io_uring_params params;
    ::memset(&params, 0, sizeof(params));

    params.cq_entries = que_depth * 2;
    params.flags = (IORING_SETUP_SINGLE_ISSUER | IORING_SETUP_CQSIZE);

    int ret = ::io_uring_queue_init_params(que_depth, uring, &params);
    ASSERT(ret == 0, "io_uring_queue_init failed: {}, {}", -ret, ::strerror(-ret));

    const auto BUF_COUNT = Conf::instance()->br_buf_count();
    // 必须是2的幂次方校验
    ASSERT(BUF_COUNT > 0 && (BUF_COUNT & (BUF_COUNT - 1)) == 0, 
        "br_buf_count 必须是 2 的幂次方 (如 16, 32, 64)");

    const auto BUF_SIZE = Conf::instance()->br_buf_size();
    const auto BR_SIZE = sizeof(io_uring_buf) * BUF_COUNT;
    const auto TOTAL_SIZE = BUF_COUNT * BUF_SIZE + BR_SIZE;

    // 申请 10MB+ 的缓冲区时, 应该使用 ::mmap 申请内存页, posix_memalign 适合小缓冲区的场景
    void* ptr = ::mmap(nullptr, TOTAL_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
    ASSERT(ptr && ptr != MAP_FAILED, 
        "::mmap(nullptr, BR_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0) failed");

    io_uring_buf_reg reg{
        .ring_addr = (uint64_t)ptr,
        .ring_entries = (uint32_t)BUF_COUNT,
        .bgid = 1,
        .flags = 0,
        .resv = 0,
    };

    ret = ::io_uring_register_buf_ring(uring, &reg, 0);
    ASSERT(ret == 0, "io_uring_register_buf_ring failed: {}, {}", -ret, ::strerror(-ret));

    io_uring_buf_ring* br = (io_uring_buf_ring*)ptr;
    ::io_uring_buf_ring_init(br);

    for (int i = 0; i < BUF_COUNT; ++i) {
        auto buf = (uint8_t*)ptr + BR_SIZE + (i * BUF_SIZE);
        brbufs.emplace_back(buf);
        // 初始化时，按照索引 i 放入对应的 slot
        ::io_uring_buf_ring_add(br, buf, BUF_SIZE, (uint16_t)i, ::io_uring_buf_ring_mask(BUF_COUNT), i);
    }
    ::io_uring_buf_ring_advance(br, BUF_COUNT);

    return br;
}


void
xq::net::release_io_uring_with_br(io_uring* uring, io_uring_buf_ring* br, std::vector<uint8_t*>& brbufs) noexcept {
    const auto BUF_COUNT = Conf::instance()->br_buf_count();
    const auto BUF_SIZE = Conf::instance()->br_buf_size();
    const auto BR_SIZE = sizeof(io_uring_buf) * BUF_COUNT;
    const auto TOTAL_SIZE = BUF_COUNT * BUF_SIZE + BR_SIZE;

    int ret = ::io_uring_unregister_buf_ring(uring, 1);
    ASSERT(ret == 0, "io_uring_unregister_buf_ring failed: [{}] {}", -ret, ::strerror(-ret));
    
    ::io_uring_queue_exit(uring);

    ASSERT(!::munmap(br, TOTAL_SIZE), "[{}] {}", errno, ::strerror(errno));
    brbufs.clear();
}


void
xq::net::recycle_buf_ring(io_uring_buf_ring* br, uint8_t* buf, uint16_t bid) noexcept {
    static const int BUF_SIZE = xq::net::Conf::instance()->br_buf_size();
    static const int BUF_COUNT = xq::net::Conf::instance()->br_buf_count();

    ::io_uring_buf_ring_add(br, buf, BUF_SIZE, (uint16_t)bid, ::io_uring_buf_ring_mask(BUF_COUNT), 0);
    ::io_uring_buf_ring_advance(br, 1);
}