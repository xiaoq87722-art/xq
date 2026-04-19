#ifndef __XQ_NET_EVENT_HPP__
#define __XQ_NET_EVENT_HPP__


#include "xq/net/net.in.hpp"


namespace xq::net {


class Reactor;
class Session;
class Listener;


struct Context {
    Context(Reactor* r, Session* s) : reactor(r), session(s)
    {}


    Reactor* reactor;
    Session* session;


    int
    send(const char* data, size_t len) noexcept;
};


class IService {
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
}; // class IService;


struct Event {
    enum class Command {
        Accept = 1,
        Send = 2,
        Broadcast = 3,
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