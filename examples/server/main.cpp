#include "xq/xq.h"
#include <gperftools/profiler.h>


static bool ALLOW_BROADCAST = false;


class EchoService : public xq::net::IService {
public:
    virtual void
    on_start(xq::net::Listener* l) override {
        // xINFO("{} start", l->to_string());
    }


    virtual void
    on_stop(xq::net::Listener* l) override {
        // xINFO("{} stop", l->to_string());
    }


    virtual int
    on_connected(xq::net::Session* s) override {
        // xINFO("{} connected", s->to_string());
        return 0;
    }


    virtual void
    on_disconnected(xq::net::Session* s) override {
        // xINFO("{} disconnected", s->to_string());
    }


    virtual int
    on_data(xq::net::Context* ctx, const char* data, size_t len) override {
        if (ALLOW_BROADCAST && len % 5 == 0) {
            ctx->session->broadcast(data, len);
        } else {
            ctx->send(data, len);
        }
        return 0;
    }
};


int
main(int, char**) {
    xq::utils::disable_log();
    ProfilerStart("./server.prof");
    EchoService echo;
    auto l1 = xq::net::Listener(&echo, "0.0.0.0", 8888);
    auto l2 = xq::net::Listener(&echo, "0.0.0.0", 9999);

    xq::net::Acceptor::instance()->run({ &l1, &l2 });
    ProfilerStop();
    return 0;
}