#include "xq/xq.h"


class EchoEvent: public xq::net::ListenerEvent {
public:
    virtual void
    on_init(xq::net::Listener* l) override {
        xINFO("✅ {} 开启监听 ✅", l->to_string());
    }


    virtual void
    on_stopped(xq::net::Listener* l) override {
        xINFO("❎ {} 关闭监听 ❎", l->to_string());
    }


    virtual int
    on_data(xq::net::Session* s, const xq::net::Buffer& buf) override {
        s->send(s->reactor(), (uint8_t*)buf.data, buf.len);
        return 0;
    }
};


int
main(int, char**) {
    auto echo = new EchoEvent;
    auto l1 = new xq::net::Listener(echo, ":8888");
    auto l2 = new xq::net::Listener(echo, ":9999");

    auto ls = std::vector<xq::net::Listener*>{ l1, l2 };

    xq::net::Acceptor::instance()->run(ls);

    delete l1;
    delete l2;
    delete echo;
    return 0;
}