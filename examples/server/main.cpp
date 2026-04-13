#include "xq/net/acceptor.hpp"


class EchoService : public xq::net::IService {
public:
    virtual void
    on_start(xq::net::Listener* l) override {
        xINFO("{} start", l->to_string());
    }


    virtual void
    on_stop(xq::net::Listener* l) override {
        xINFO("{} stop", l->to_string());
    }


    virtual void
    on_connected(xq::net::Session* s) override {
        xINFO("{} connected", s->to_string());
    }


    virtual void
    on_disconnected(xq::net::Session* s) override {
        xINFO("{} disconnected", s->to_string());
    }


    virtual void
    on_data(xq::net::Session* s, const char* data, size_t len) override {
        s->send(const_cast<char*>(data), len);
    }
};


int
main(int, char**) {
    EchoService echo;
    auto l1 = xq::net::Listener(&echo, "0.0.0.0", 8888);
    auto l2 = xq::net::Listener(&echo, "0.0.0.0", 9999);

    xq::net::Acceptor::instance()->run({ &l1, &l2 });
    return 0;
}