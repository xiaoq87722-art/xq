#ifndef __XQ_NET_WRITE_BUF_HPP__
#define __XQ_NET_WRITE_BUF_HPP__


#include "xq/net/net.in.h"
#include <stack>


namespace xq::net {


class Reactor;


struct WriteBuf {
    uv_write_t req;
    Reactor* reactor;
    char data[WBUF_MAX];


    struct Pool {
        WriteBuf*
        get() noexcept {
            WriteBuf* res = nullptr;
            if (pool.empty()) {
                res = (WriteBuf*)xq::utils::malloc(sizeof(WriteBuf));
            } else {
                res = pool.top();
                pool.pop();
            }
            ::memset(&res->req, 0, sizeof(res->req));
            return res;
        }


        void
        put(WriteBuf* req) noexcept {
            pool.push(req);
        }


        void
        clear() noexcept {
            while (!pool.empty()) {
                WriteBuf* req = pool.top();
                pool.pop();
                xq::utils::free(req);
            }
        }


    private:
        std::stack<WriteBuf*> pool;
    };
}; // struct WriteReq;

    
} // namespace xq::net



#endif // __XQ_NET_WRITE_BUF_HPP__