#include "xq/utils/log.hpp"

int
main(int, char**) {
    // 指定日志目录（可选，默认为当前目录）
    xq::utils::init_log();
    
    xDEBUG("This goes to logs");
    xINFO("This goes to logs");
    xWARN("This goes to logs");
    xERROR("This goes to logs");
    xFATAL("This goes to logs");
    
    return 0;
}