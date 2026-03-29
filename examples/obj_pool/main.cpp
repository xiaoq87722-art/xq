#include "xq/utils/obj_pool.hpp"
#include <iostream>


int
main(int, char**) {
    xq::utils::ObjPool<int> pool;

    {
        auto h1 = pool.acquire(42);
        auto h2 = pool.acquire(100);
        auto h3 = pool.acquire(200);

        if (h1 == h2) {
            std::cout<< "Hello world" <<std::endl;
        }

        std::cout << *h1 << ", " << *h2 << ", " << *h3 << std::endl;
    }

    {
        auto h4 = pool.acquire(300);
        std::cout << *h4 << std::endl;
    }

    return 0;
}