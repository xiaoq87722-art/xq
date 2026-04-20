#ifndef __XQ_NET_EVENT_HPP__
#define __XQ_NET_EVENT_HPP__


#include "xq/net/net.in.hpp"


namespace xq::net {


class Reactor;
class Session;
class Listener;
class Conn;


struct Context {
    Context(Reactor* r, Session* s) : reactor(r), session(s)
    {}


    Reactor* reactor;
    Session* session;


    int
    send(const char* data, size_t len) noexcept;
}; // struct Context;


class IConnEvent {
public:
    virtual int
    on_connected(Conn* c) = 0;


    virtual void
    on_disconnected(Conn* c) = 0;


    virtual int
    on_data(Conn* c, const char* data, size_t len) = 0;
}; // class IConnEvent;


class IListnerEvent {
public:
    virtual void
    on_start(Listener* l) = 0;


    virtual void
    on_stop(Listener* l) = 0;


    virtual int
    on_connected(Session* s) = 0;


    virtual void
    on_disconnected(Session* s) = 0;


    virtual int
    on_data(Context* ctx, const char* data, size_t len) = 0;
}; // class IListnerEvent;


struct Event {
    enum class Command {
        Accept = 1,    // 有新的连接到达 Acceptor
        Send = 2,      // 有发送数据到达 Reactor
        Connect = 3,   // Conn 连接成功
        Broadcast = 4, // Acceptor 广播
        Proc = 5,      // Connector 有数据到达需要处理
    };


    Command cmd;
    void* data;
};


struct EventAcceptParam {
    SOCKET fd;
    Listener* l;
};


struct EventBroadcastParam {
    size_t len;
    char data[];
};


} // namespace xq::net


#endif // __XQ_NET_EVENT_HPP__