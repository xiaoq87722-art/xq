#ifndef __XQ_NET_BUFFER__
#define __XQ_NET_BUFFER__


#include <stdint.h>
#include <memory>
#include <algorithm>
#include "xq/utils/memory.hpp"
#include "xq/utils/log.hpp"


namespace xq {
namespace net {


class Buffer {
    Buffer(const Buffer&) = delete;
    Buffer(Buffer&&) = delete;
    Buffer& operator=(const Buffer&) = delete;
    Buffer& operator=(Buffer&&) = delete;


public:
    explicit Buffer() noexcept {
        data_ = (uint8_t*)xq::utils::malloc(cap_);
        ASSERT(data_, "xq::utils::malloc({}) failed", cap_);
    }


    ~Buffer() noexcept {
        xq::utils::free(data_);
    }


    const uint8_t*
    data() const {
        return data_ + start_;
    }


    uint32_t
    len() const {
        return end_ - start_;
    }


    uint32_t
    cap() const {
        return cap_;
    }


    void
    append(const uint8_t* data, uint32_t datalen) noexcept {
        if (end_ + datalen > cap_) {
            uint32_t logical_len = len();
            if (logical_len + datalen <= cap_) {
                ::memmove(data_, data_ + start_, logical_len);
                start_ = 0;
                end_ = logical_len;
            } else {
                uint32_t new_cap = std::max(cap_ * 2, logical_len + datalen);
                auto tmp = (uint8_t*)xq::utils::malloc(new_cap);
                ASSERT(tmp, "malloc failed");
                if (logical_len > 0) {
                    ::memcpy(tmp, data_ + start_, logical_len);
                }
                xq::utils::free(data_);
                data_ = tmp;
                cap_ = new_cap;
                start_ = 0;
                end_ = logical_len;
            }
        }

        ::memcpy(data_ + end_, data, datalen);
        end_ += datalen;
    }


    void
    set_data(const uint8_t* data, uint32_t datalen) noexcept {
        ::memcpy(data_, data, datalen);
        end_ = datalen;
        start_ = 0;
    }


    void
    consume(uint32_t n) noexcept {
        n = std::min(n, len());
        start_ += n;
        if (start_ == end_) {
            reset();
        }
    }


    void
    reset() noexcept {
        start_ = end_ = 0;
    }


private:
    /** 数据指针 */
    uint8_t* data_ { nullptr };

    /** 数据开始位置 */
    uint32_t start_ { 0 };

    /** 数据终止位置 */
    uint32_t end_ { 0 };

    /** data_ 总大小 */
    uint32_t cap_ { 1024 };
}; // class Buffer;

    
} // namespace net
} // namespace xq


#endif // __XQ_NET_BUFFER__