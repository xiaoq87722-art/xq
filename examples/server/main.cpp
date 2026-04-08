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
    on_data(xq::net::Session* s, const uint8_t* data, size_t len) override {
        s->send(s->reactor(), (uint8_t*)data, len);
        return 0;
    }


    virtual int
    on_connected(xq::net::Session* s) override {
        xINFO("{} 连接成功 ✅", s->to_string());
        return 0;
    }


    virtual void
    on_disconnected(xq::net::Session* s) override {
        xINFO("服务端 {} {} 断开连接 ❎", s->closed_by_server() ? "主动" : "被动", s->to_string());
    }
};


int
main(int, char**) {
    auto echo = new EchoEvent;

    auto ls = std::vector<xq::net::Listener*> { 
        new xq::net::Listener(echo, ":8888"), 
        new xq::net::Listener(echo, ":9999"),
    };

    xq::net::Acceptor::instance()->run(ls);

    for (auto l: ls) {
        delete l;
    }

    delete echo;
    return 0;
}