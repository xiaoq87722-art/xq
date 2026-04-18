#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>


#include <atomic>
#include <chrono>
#include <cstring>
#include <format>
#include <iostream>
#include <thread>
#include <vector>


// ─── Config ───────────────────────────────────────────────────────────────────
static int         g_conns    = 100;
static int         g_threads  = 2;
static int         g_msgsize  = 64;
static int         g_rate     = 100;   // msg/s per connection
static int         g_duration = 30;
static const char* g_host     = "127.0.0.1";
static int         g_port     = 8888;

// ─── Stats ────────────────────────────────────────────────────────────────────
static std::atomic<uint64_t> g_sent {0};
static std::atomic<uint64_t> g_recv {0};
static std::atomic<uint64_t> g_err  {0};

// 延迟分桶：<100us / <1ms / <10ms / <100ms / >=100ms
static std::atomic<uint64_t> g_lat[5] {};

static inline uint64_t now_ns() {
    return (uint64_t)std::chrono::steady_clock::now().time_since_epoch().count();
}

static int lat_bucket(uint64_t us) {
    if (us < 100)    return 0;
    if (us < 1000)   return 1;
    if (us < 10000)  return 2;
    if (us < 100000) return 3;
    return 4;
}

// ─── Connection ───────────────────────────────────────────────────────────────
struct Conn {
    int            fd        { -1 };
    bool           connected { false };
    uint64_t       next_send { 0 };
    std::vector<char> rbuf;
    int            rlen      { 0 };
};

static int make_conn(const char* host, int port) {
    int fd = ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;

    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_port   = htons(port);
    ::inet_pton(AF_INET, host, &addr.sin_addr);

    int r = ::connect(fd, (sockaddr*)&addr, sizeof(addr));
    if (r < 0 && errno != EINPROGRESS) {
        ::close(fd);
        return -1;
    }
    return fd;
}

// ─── Worker ───────────────────────────────────────────────────────────────────
static void worker(int conn_start, int conn_end, std::atomic<bool>& stop) {
    int epfd = ::epoll_create1(0);

    const uint64_t interval_ns = 1'000'000'000ULL / (uint64_t)g_rate;
    const int      bufsz       = g_msgsize * 4;

    std::vector<Conn> conns(conn_end - conn_start);

    for (int i = 0; i < (int)conns.size(); ++i) {
        auto& c  = conns[i];
        c.rbuf.resize(bufsz);
        c.fd     = make_conn(g_host, g_port);
        if (c.fd < 0) { ++g_err; continue; }

        // 错开各连接的首次发送时间，避免同时突发
        c.next_send = now_ns() + (uint64_t)i * interval_ns / conns.size();

        epoll_event ev;
        ev.events   = EPOLLOUT | EPOLLET;   // 等 connect 完成
        ev.data.ptr = &c;
        ::epoll_ctl(epfd, EPOLL_CTL_ADD, c.fd, &ev);
    }

    std::vector<char>       msg(g_msgsize, 0xAB);
    std::vector<epoll_event> events(256);

    while (!stop.load(std::memory_order_relaxed)) {
        // 计算最近一次需要发包的等待时间
        uint64_t now        = now_ns();
        uint64_t next_wake  = now + 10'000'000ULL;  // 最多等 10ms
        for (auto& c : conns) {
            if (c.fd >= 0 && c.connected && c.next_send < next_wake)
                next_wake = c.next_send;
        }

        int timeout_ms = (int)((next_wake - now) / 1'000'000);
        if (timeout_ms < 0) timeout_ms = 0;

        int n = ::epoll_wait(epfd, events.data(), (int)events.size(), timeout_ms);
        now   = now_ns();

        for (int i = 0; i < n; ++i) {
            auto& ev = events[i];
            auto* c  = (Conn*)ev.data.ptr;
            if (c->fd < 0) continue;

            if (!c->connected) {
                int err = 0; socklen_t len = sizeof(err);
                ::getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &len);
                if (err) {
                    ::close(c->fd); c->fd = -1; ++g_err;
                    continue;
                }
                c->connected = true;
                epoll_event mod;
                mod.events   = EPOLLIN | EPOLLET;
                mod.data.ptr = c;
                ::epoll_ctl(epfd, EPOLL_CTL_MOD, c->fd, &mod);
                continue;
            }

            if (ev.events & EPOLLIN) {
                while (1) {
                    int r = ::recv(c->fd, c->rbuf.data() + c->rlen,
                                   bufsz - c->rlen, 0);
                    if (r <= 0) break;
                    c->rlen += r;

                    while (c->rlen >= g_msgsize) {
                        uint64_t sent_ns = 0;
                        memcpy(&sent_ns, c->rbuf.data(), sizeof(uint64_t));
                        uint64_t rtt_us = (now_ns() - sent_ns) / 1000;
                        g_lat[lat_bucket(rtt_us)].fetch_add(1, std::memory_order_relaxed);
                        ++g_recv;

                        c->rlen -= g_msgsize;
                        if (c->rlen > 0)
                            memmove(c->rbuf.data(), c->rbuf.data() + g_msgsize, c->rlen);
                    }
                }
            }

            if (ev.events & (EPOLLERR | EPOLLHUP)) {
                ::close(c->fd); c->fd = -1; ++g_err;
            }
        }

        // 发包：扫描所有已到期的连接
        now = now_ns();
        for (auto& c : conns) {
            if (c.fd < 0 || !c.connected || now < c.next_send) continue;

            uint64_t ts = now_ns();
            memcpy(msg.data(), &ts, sizeof(uint64_t));

            int r = ::send(c.fd, msg.data(), g_msgsize, 0);
            if (r == g_msgsize) {
                ++g_sent;
                c.next_send = now + interval_ns;
            } else if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                ::close(c.fd); c.fd = -1; ++g_err;
            }
        }
    }

    for (auto& c : conns) {
        if (c.fd >= 0) ::close(c.fd);
    }
    ::close(epfd);
}

// ─── Main ─────────────────────────────────────────────────────────────────────
int main(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        std::string a = argv[i];
        if      (a == "-c" && i+1 < argc) g_conns    = atoi(argv[++i]);
        else if (a == "-t" && i+1 < argc) g_threads  = atoi(argv[++i]);
        else if (a == "-r" && i+1 < argc) g_rate     = atoi(argv[++i]);
        else if (a == "-s" && i+1 < argc) g_msgsize  = atoi(argv[++i]);
        else if (a == "-d" && i+1 < argc) g_duration = atoi(argv[++i]);
        else if (a == "-h" && i+1 < argc) g_host     = argv[++i];
        else if (a == "-p" && i+1 < argc) g_port     = atoi(argv[++i]);
        else if (a == "--help") {
            std::cout <<
                "usage: client [options]\n"
                "  -c <n>   connections   (default 100)\n"
                "  -t <n>   worker threads(default 2)\n"
                "  -r <n>   msg/s per conn(default 100)\n"
                "  -s <n>   msg size bytes(default 64)\n"
                "  -d <n>   duration secs (default 30)\n"
                "  -h <ip>  server host   (default 127.0.0.1)\n"
                "  -p <n>   server port   (default 8888)\n";
            return 0;
        }
    }

    if (g_msgsize < 8) g_msgsize = 8;
    if (g_threads > g_conns) g_threads = g_conns;

    std::cout << std::format(
        "bench: {} conns / {} threads / {} msg/s per conn / {} B / {}s → {}:{}\n",
        g_conns, g_threads, g_rate, g_msgsize, g_duration, g_host, g_port);
    std::cout << std::format(
        "total target: {} msg/s\n\n", (uint64_t)g_conns * g_rate);

    std::atomic<bool> stop {false};
    std::vector<std::thread> threads;
    threads.reserve(g_threads);

    int per = g_conns / g_threads;
    for (int i = 0; i < g_threads; ++i) {
        int start = i * per;
        int end   = (i == g_threads - 1) ? g_conns : start + per;
        threads.emplace_back(worker, start, end, std::ref(stop));
    }

    uint64_t prev_sent = 0, prev_recv = 0;
    for (int t = 0; t < g_duration; ++t) {
        std::this_thread::sleep_for(std::chrono::seconds(1));

        uint64_t sent = g_sent.load();
        uint64_t recv = g_recv.load();
        uint64_t err  = g_err.load();
        std::cout << std::format("[{:3d}s] sent {:8d}/s  recv {:8d}/s  err {}\n",
            t + 1, sent - prev_sent, recv - prev_recv, err);

        prev_sent = sent;
        prev_recv = recv;
    }

    stop.store(true);
    for (auto& t : threads) t.join();

    // 汇总延迟分布
    uint64_t total = g_recv.load();
    std::cout << "\n─── Latency (RTT) distribution ─────────────────\n";
    const char* labels[] = { "<100us", "<1ms", "<10ms", "<100ms", ">=100ms" };
    for (int i = 0; i < 5; ++i) {
        uint64_t cnt = g_lat[i].load();
        double   pct = total > 0 ? 100.0 * (double)cnt / (double)total : 0.0;
        std::cout << std::format("  {:8s}: {:8d}  ({:.1f}%)\n", labels[i], cnt, pct);
    }
    std::cout << std::format(
        "\ntotal sent: {}  recv: {}  err: {}\n",
        g_sent.load(), total, g_err.load());

    return 0;
}
