#include "xq/net/acceptor.hpp"
#include "xq/net/reactor.hpp"
#include "xq/utils/signal.h"
#include <emmintrin.h>
#include <liburing.h>
#include "xq/utils/log.hpp"
#include <cstring>
#include "xq/net/conf.hpp"


static void
signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        xq::net::Acceptor::instance()->stop();
    }
}


static xq::net::Reactor*
get_reactor(const std::vector<xq::net::Reactor*>& reactors) {
    if (reactors.empty()) return nullptr;

    auto reactor = reactors[0];
    uint64_t min_load = reactor->loaded();

    for (size_t i = 1, n = reactors.size(); i < n; ++i) {
        uint64_t cur_load = reactors[i]->loaded();
        if (cur_load < min_load) {
            reactor = reactors[i];
            min_load = cur_load;
        }
    }

    return reactor;
}


static void
init_reactors(std::vector<xq::net::Reactor*>& reactors, std::vector<std::thread>& threads) {
    uint32_t nthread = std::thread::hardware_concurrency();

    for (uint32_t i = 0; i < nthread; ++i) {
        auto reactor = new xq::net::Reactor();
        reactors.emplace_back(reactor);
        threads.emplace_back(std::bind(&xq::net::Reactor::run, reactor));
    }

    for (auto& reactor: reactors) {
        while (!reactor->running()) {
            ::_mm_pause();
        }
    }
}


static void
release_reactors(std::vector<xq::net::Reactor*>& reactors, std::vector<std::thread>& threads) {
    for (auto& thread : threads) {
        if (thread.joinable()) {
            thread.join();
        }
    }

    for (auto* r: reactors) {
        delete r;
    }

    threads.clear();
    reactors.clear();
}


xq::net::Acceptor::~Acceptor() noexcept {
    for (auto* s: sess_slots_) {
        if (s) {
            delete s;
        }
    }
}


void
xq::net::Acceptor::run(const std::initializer_list<const char*>& endpoints) noexcept {
    // Step 1, 检查Acceptor是否已启动
    int stopped = STATE_STOPPED;
    if (!state_.compare_exchange_strong(stopped, STATE_STARTING)) {
        return;
    }

    std::vector<std::thread> threads;
    std::vector<Reactor*> reactors;

    int ret = 0;
    // Step 2, 初始化io_uring
    const auto que_depth = xq::net::Conf::instance()->que_depth();
    ret = ::io_uring_queue_init(que_depth, &uring_, IORING_SETUP_SINGLE_ISSUER);
    ASSERT(ret == 0, "io_uring_queue_init failed: {}, {}", -ret, ::strerror(-ret));

    // Step 3, 启动Reactor线程
    init_reactors(reactors, threads);

    // Step 4, 注册信号处理函数
    xq::utils::regist_signal(signal_handler, {SIGINT, SIGTERM});

    // Step 5, 启动Listener线程并添加监听事件
    auto listeners = Listener::build_listeners(&uring_, endpoints);

    io_uring_cqe* cqe = nullptr;
    SOCKET cfd = INVALID_SOCKET;
    unsigned head, count;

    // Step 6, 事件循环
    xINFO("✅ 1 acceptor 线程, {} reactor 线程 开始工作 ✅", threads.size());
    state_ = STATE_RUNNING;
    while (1) {
        ret = ::io_uring_submit_and_wait(&uring_, 1);
        if (ret < 0) {
            if (ret == -EINTR && state_ != STATE_RUNNING) {
                break;
            }

            xERROR("io_uring_submit_and_wait failed: {}, {}", -ret, ::strerror(-ret));
            continue;
        }

        count = 0;
        io_uring_for_each_cqe(&uring_, head, cqe) {
            count++;
            auto l = (Listener*)::io_uring_cqe_get_data(cqe);
            if (!l) {
                continue;
            }

            if (!(cqe->flags & IORING_CQE_F_MORE)) {
                xERROR("multishot accept stopped for {}", l->host());
            }

            cfd = cqe->res;
            if (cfd < 0) {
                xERROR("{} accept failed: {}, {}", l->host(), -cfd, ::strerror(-cfd));
                continue;
            }

            if (cfd >= sess_slots_.size()) {
                // 动态扩容以适应更高的文件描述符
                sess_slots_.resize(cfd + 1024, nullptr);
            }

            auto* s = sess_slots_[cfd];
            if (!s) {
                s = new Session;
                sess_slots_[cfd] = s;
            }

            auto* r = get_reactor(reactors);
            s->init(cfd, l, r);

            auto ev = r->ev_pool().acquire_event();
            ev->init(RingCommand::S_ACCEPT, cfd, s);
            r->notify(&uring_, ev);
        }

        if (count > 0) {
            ::io_uring_cq_advance(&uring_, count);
        }

        if (state_ != STATE_RUNNING) {
            break;
        }
    }

    // Step 7, 停止监听
    Listener::release_listeners(listeners);

    // Step 8, 停止 reactor 线程
    for (auto* r: reactors) {
        auto ev = r->ev_pool().acquire_event();
        ev->init(RingCommand::R_STOP);
        r->notify(&uring_, ev, true);
    }

    // Step 9, 释放 reactors 和 threads
    release_reactors(reactors, threads);

    // Step 10, 释放 io_uring
    ::io_uring_queue_exit(&uring_);

    xINFO("❎ 服务关闭 ❎");
    state_ = STATE_STOPPED;
}