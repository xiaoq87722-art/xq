#include "xq/net/acceptor.hpp"

int
main(int, char**) {
    auto l1 = xq::net::Listener("0.0.0.0", 8888);
    auto l2 = xq::net::Listener("0.0.0.0", 9999);

    xq::net::Acceptor::instance()->run({ &l1, &l2 });
    return 0;
}