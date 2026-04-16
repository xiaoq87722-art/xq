#ifndef __XQ_NET_EVENT_HPP__
#define __XQ_NET_EVENT_HPP__


#include "xq/net/net.in.h"
#include <utility>


namespace xq::net {


class Session;
class Listener;


constexpr int EE_TYPE_LISTENER = 1;
constexpr int EE_TYPE_QUEUE = 2;
constexpr int EE_TYPE_SESSION = 3;


constexpr int EE_CMD_ACCEPT = 1;
constexpr int EE_CMD_STOP = 2;
constexpr int EE_CMD_SEND = 3;


struct EpollArg {
    int type;
    int cmd;
    void* data;
};


struct OnAcceptArg {
    SOCKET fd;
    Listener* l;
};


constexpr int EVENT_ON_SEND = 3;
struct OnSendArg {
    Session* s;
    size_t len;
    char data[];
};


} // namespace xq::net


#endif // __XQ_NET_EVENT_HPP__