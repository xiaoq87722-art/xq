#ifndef __XQ_NET_EVENT_HPP__
#define __XQ_NET_EVENT_HPP__


#include "xq/net/net.in.hpp"
#include "xq/utils/ring_buf.hpp"


namespace xq::net {


class Conn;
class Listener;
class Reactor;
class Session;


/** 连接端事件 */
class IConnEvent {
public:
    /** 与服务端成功建立连接时触发 */
    virtual int
    on_connected(Conn* c) = 0;


    /** 与服务端连接断开时触发 */
    virtual void
    on_disconnected(Conn* c) = 0;


    /** 接收数据时触发 */
    virtual int
    on_data(Conn* c, xq::utils::RingBuf& rbuf) = 0;
}; // class IConnEvent;


/** 监听端事件 */
class IListnerEvent {
public:
    /** 监听端启动时触发 */
    virtual void
    on_start(Listener* l) = 0;


    /** 监听停止时触发 */
    virtual void
    on_stop(Listener* l) = 0;


    /** 会话连接成功时触发 */
    virtual int
    on_connected(Session* s) = 0;


    /** 会话连接失败时触发 */
    virtual void
    on_disconnected(Session* s) = 0;


    /** 接收到会话数据时触发 */
    virtual int
    on_data(Session* s, xq::utils::RingBuf& rbuf) = 0;
}; // class IListnerEvent;


/**
 * @brief 事件, 该事件对象用于 EventQueue.
 * 
 *        拥有事件管道的对象有: reactor, processor 和 sender
 */
struct Event {
    enum class Type {
        Accept = 1,    // 有新的连接到达 Acceptor
        Send = 2,      // 有发送数据到达 Reactor
        Connect = 3,   // Conn 连接成功
        Broadcast = 4, // Acceptor 广播
    };


    /** 事件类型 */
    Type type;

    /** 事件参数 */
    void* param;
};


/** Accept 事件参数 */
struct EAcceptParam {
    SOCKET fd;
    Listener* l;
};


/** 广播事件参数 */
struct EBroadcastParam {
    size_t len;
    char data[];
};


} // namespace xq::net


#endif // __XQ_NET_EVENT_HPP__