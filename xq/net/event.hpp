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
    virtual void on_start(Listener* l) = 0;
    virtual void on_stop(Listener* l) = 0;
    virtual int on_connected(Session* s) = 0;
    virtual void on_disconnected(Session* s) = 0;
    virtual int on_data(Context* ctx, const char* data, size_t len) = 0;
}; // class IService;


constexpr int EA_TYPE_LISTENER = 1;
constexpr int EA_TYPE_QUEUE = 2;
constexpr int EA_TYPE_SESSION = 3;


struct EpollArg {
    int type;
    void* data;
};


constexpr int EV_CMD_ACCEPT = 1;
constexpr int EV_CMD_STOP = 2;
constexpr int EV_CMD_SEND = 3;
constexpr int EV_CMD_BROADCAST = 4;


struct Event {
    int cmd;
    void* data;
};


struct OnAcceptArg {
    SOCKET fd;
    Listener* l;
};


struct OnBroadcastArg {
    size_t len;
    char data[];
};


} // namespace xq::net


#endif // __XQ_NET_EVENT_HPP__