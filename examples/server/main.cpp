#include "xq/xq.h"
#include <gperftools/profiler.h>


static bool ALLOW_BROADCAST = false;


class EchoService : public xq::net::IListnerEvent {
public:
    virtual void
    on_start(xq::net::Listener* l) override {
        xINFO("{} start", l->to_string());
    }


    virtual void
    on_stop(xq::net::Listener* l) override {
        xINFO("{} stop", l->to_string());
    }


    virtual int
    on_connected(xq::net::Session* s) override {
        xINFO("{} connected", s->to_string());
        return 0;
    }


    virtual void
    on_disconnected(xq::net::Session* s) override {
        xINFO("{} {} disconnected", s->closed_by_server() ? "主动" : "被动", s->to_string());
    }


    virtual int
    on_data(xq::net::Context* ctx, xq::utils::RingBuf& rbuf) override {
        iovec iov[2];
        int niov = rbuf.read_iov(iov);
        size_t total = 0;
        for (int i = 0; i < niov; ++i) {
            size_t n = iov[i].iov_len;
            if (ALLOW_BROADCAST && n % 5 == 0) {
                ctx->session->broadcast((char*)iov[i].iov_base, n);
            } else {
                ctx->send((char*)iov[i].iov_base, n);
            }
            total += n;
        }
        rbuf.read_consume(total);
        return 0;
    }
};


int
main(int, char**) {
#if (!NDEBUG)
    xq::utils::disable_log();
    ProfilerStart("./server.prof");
#endif

    EchoService echo;
    auto l1 = xq::net::Listener(&echo, "0.0.0.0", 8888);
    auto l2 = xq::net::Listener(&echo, "0.0.0.0", 9999);

    xq::net::Acceptor::instance()->run({ &l1, &l2 });
    
#if (!NDEBUG)
    ProfilerStop();
#endif
    return 0;
}