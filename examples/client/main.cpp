#include "xq/net/connector.hpp"


int
main(int, char**) {
    xq::net::Connector::instance()->run({"127.0.0.1:8888"});
    return 0;
}