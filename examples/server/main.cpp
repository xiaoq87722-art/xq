#include "xq/net/acceptor.hpp"


int
main(int, char**) {
    xq::net::Acceptor::instance()->run({":8888"});
    return 0;
}