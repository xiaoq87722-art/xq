#include "xq/net/buffer.hpp"


void
xq::net::Buffer::append(const uint8_t* data, uint32_t datalen) noexcept {
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