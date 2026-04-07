#ifndef __XQ_NET_BUFFER__
#define __XQ_NET_BUFFER__


#include <stdint.h>
#include <memory>
#include <sys/socket.h>
#include "xq/utils/memory.hpp"
#include "xq/utils/log.hpp"


namespace xq {
namespace net {


struct Buffer {
    void* data { nullptr };
    uint32_t len { 0 };


    Buffer(const Buffer& other) = delete;
    Buffer& operator=(const Buffer& other) = delete;

    Buffer() {}


    Buffer(const void* data, uint32_t datalen) {
        set_data(data, datalen);
    }


    ~Buffer() {
        if (data) {
            xq::utils::free(data);
        }
    }


    Buffer(Buffer&& other) 
        : data(other.data), len(other.len) {
            other.data = nullptr;
            other.len = 0;
    }


    Buffer&
    operator=(Buffer&& other) {
        if (this != &other) {
            if (this->data) {
                xq::utils::free(this->data);
            }

            this->data = other.data;
            this->len = other.len;

            other.data = nullptr;
            other.len = 0;
        }

        return *this;
    }


    operator iovec() {
        iovec iov;
        iov.iov_base = data;
        iov.iov_len = len;

        data = nullptr;
        len = 0;

        return std::move(iov);
    }


    void
    set_data(const void* data, uint32_t datalen) {
        if (!this->data) {
            this->data = xq::utils::malloc(datalen);
        }

        ::memcpy(this->data, data, datalen);
        this->len = datalen;
    }
};


struct SendBuf {
    msghdr   mh {};
    uint32_t total { 0 };
};

    
} // namespace net
} // namespace xq


#endif // __XQ_NET_BUFFER__