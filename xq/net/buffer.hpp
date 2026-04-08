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
    uint32_t cap { 0 };


    Buffer(const Buffer& other) = delete;
    Buffer& operator=(const Buffer& other) = delete;

    Buffer() {}


    Buffer(const void* data, uint32_t datalen) {
        set_data(data, datalen); 
    }


    ~Buffer() {
        if (data) {
            xq::utils::free(data);
            data = nullptr;
        }
    }


    Buffer(Buffer&& other) 
        : data(other.data), len(other.len), cap(other.cap) { // 移动构造函数也要移动 cap
            other.data = nullptr;
            other.len = 0;
            other.cap = 0; // 确保源对象的 cap 也被清零
    }


    Buffer&
    operator=(Buffer&& other) {
        if (this != &other) {
            if (this->data) {
                xq::utils::free(this->data);
            }

            this->data = other.data;
            this->len = other.len;
            this->cap = other.cap;

            other.data = nullptr;
            other.len = 0;
            other.cap = 0;
        }

        return *this;
    }


    operator iovec() {
        iovec iov;
        iov.iov_base = data;
        iov.iov_len = len;

        data = nullptr;
        len = 0;
        cap = 0;

        return iov;
    }


    void
    set_data(const void* data, uint32_t datalen) {
        ASSERT(datalen > 0 && data, "无效的参数");

        if (datalen > cap) {
            if (this->data) {
                xq::utils::free(this->data);
            }

            this->data = (datalen > 0) ? xq::utils::malloc(datalen) : nullptr;
            this->cap = datalen;
        }

        this->len = datalen;
        ::memcpy(this->data, data, datalen);
    }
};


struct SendBuf {
    msghdr   mh {};
    uint32_t total { 0 };
};

    
} // namespace net
} // namespace xq


#endif // __XQ_NET_BUFFER__