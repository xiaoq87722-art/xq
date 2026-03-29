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


void*
xq::net::init_io_uring_with_br(io_uring* uring, std::vector<uint8_t*> &brbufs) noexcept {
    const auto que_depth = xq::net::Conf::instance()->que_depth();
    int ret = ::io_uring_queue_init(que_depth, uring, IORING_SETUP_SINGLE_ISSUER);
    ASSERT(ret == 0, "io_uring_queue_init failed: {}, {}", -ret, ::strerror(-ret));

    const auto BUF_COUNT = Conf::instance()->br_buf_count();
    const auto BUF_SIZE = Conf::instance()->br_buf_size();
    const auto BR_SIZE = BUF_COUNT * BUF_SIZE;

    // 申请 10MB+ 的缓冲区时, 应该使用 ::mmap 申请内存页, posix_memalign 适合小缓冲区的场景
    void* ptr = ::mmap(nullptr, BR_SIZE, PROT_READ|PROT_WRITE, MAP_PRIVATE|MAP_ANONYMOUS|MAP_POPULATE, -1, 0);
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

    io_uring_buf_ring *br = (io_uring_buf_ring*)ptr;
    ::io_uring_buf_ring_init(br);

    for (int i = 0; i < BUF_COUNT; ++i) {
        auto* buf = (uint8_t*)xq::utils::malloc(BUF_SIZE);
        brbufs.emplace_back(buf);
        ::io_uring_buf_ring_add(br, buf, BUF_SIZE, i, ::io_uring_buf_ring_mask(BUF_COUNT), i);
    }
    ::io_uring_buf_ring_advance(br, BUF_COUNT);

    return ptr;
}


void
xq::net::release_io_uring_with_br(io_uring* uring, void* ptr, std::vector<uint8_t*>& brbufs) noexcept {
    const auto BR_SIZE = Conf::instance()->br_buf_count() * Conf::instance()->br_buf_size();

    int ret = ::io_uring_unregister_buf_ring(uring, 1);
    ASSERT(ret == 0, "io_uring_unregister_buf_ring failed: [{}] {}", -ret, ::strerror(-ret));
    
    ::io_uring_queue_exit(uring);

    for (auto buf: brbufs) {
        xq::utils::free(buf);
    }

    ASSERT(!::munmap(ptr, BR_SIZE), "[{}] {}", errno, ::strerror(errno));
    brbufs.clear();
}


xq::net::RingEvent::Pool::~Pool() noexcept {
    RingEvent* curr = pool_head_.load();
    while (curr) {
        RingEvent* next = curr->next;
        delete curr;
        curr = next;
    }
}


xq::net::RingEvent*
xq::net::RingEvent::Pool::acquire_event() noexcept {
    RingEvent* old_head = pool_head_.load(std::memory_order_acquire);
    while (old_head && !pool_head_.compare_exchange_weak(
        old_head, old_head->next, std::memory_order_acq_rel, std::memory_order_acquire)
    ) {
        ::_mm_pause();
    }

    if (!old_head) {
        old_head = new RingEvent();
    }

    old_head->next = nullptr;
    return old_head;
}


void
xq::net::RingEvent::Pool::release_event(RingEvent* ev) noexcept {
    if (!ev) {
        return;
    }
        
    RingEvent* old_head = pool_head_.load(std::memory_order_relaxed);
    do {
        ev->next = old_head;
    } while (!pool_head_.compare_exchange_weak(old_head, ev, std::memory_order_release, std::memory_order_relaxed));
}