#ifndef __XQ_NET_EVENT_HPP__
#define __XQ_NET_EVENT_HPP__


#include <utility>


namespace xq::net {


class Session;
class Listener;


typedef std::pair<int, void*> Event;


constexpr int EVENT_ON_STOP = 1;


constexpr int EVENT_ON_ACCEPT = 2;
struct OnAcceptArg {
    SOCKET fd;
    Listener* l;
};


constexpr int EVENT_ON_SEND = 3;
struct OnSendArg {
    Session* s;
    size_t len;
    char data[WBUF_MAX];
};


} // namespace xq::net


#endif // __XQ_NET_EVENT_HPP__