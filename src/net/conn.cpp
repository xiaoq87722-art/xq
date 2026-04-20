#include "xq/net/conn.hpp"
#include "xq/net/conn_recver.hpp"


int
xq::net::Conn::read(void* data, size_t dlen) noexcept {
    char *p = (char*)data;
    size_t nleft = dlen;

    int n, err = 0;
    while (nleft > 0) {
        n = ::recv(fd_, p, nleft, 0);
        if (n < 0) {
            err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("recv failed: [{}] {}", err, ::strerror(err));
                return -1;
            }

            return dlen - nleft;
        } else if (n == 0) {
            return 0;
        } else {
            p += n;
            nleft -= n;
        }
    }

    return dlen - nleft;
}


int
xq::net::Conn::write(const void* data, size_t dlen) noexcept {
    const char* p = (const char*)data;
    size_t nleft = dlen;

    while (nleft > 0) {
        int n = ::send(fd_, p, nleft, 0);
        if (n < 0) {
            int err = errno;
            if (err != EAGAIN && err != EWOULDBLOCK) {
                xERROR("send failed: [{}] {}", err, ::strerror(err));
                return -1;
            }
            ::_mm_pause();
        } else {
            p += n;
            nleft -= n;
        }
    }

    return dlen - nleft;
}