#ifndef __XQ_NET_EVENT_HPP__
#define __XQ_NET_EVENT_HPP__


#include <utility>


namespace xq::net {


class Listener;
typedef std::pair<int, void*> Event;


constexpr int EVENT_ON_ACCEPT = 1;
constexpr int EVENT_ON_STOP = 2;


struct OnAcceptArg {
    SOCKET fd;
    Listener* l;
};


} // namespace xq::net


#endif // __XQ_NET_EVENT_HPP__