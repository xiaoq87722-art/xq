#ifndef __XQ_UTILS_MEMORY_HPP__
#define __XQ_UTILS_MEMORY_HPP__

#include <mimalloc.h>


namespace xq {
namespace utils {


inline void* 
malloc(size_t n, bool auto_init = false) {
    if (auto_init) {
        return ::mi_malloc(n);
    }
    return ::mi_zalloc(n);
}


inline void
free(void* ptr) {
    ::mi_free(ptr);
}


inline void*
realloc(void* ptr, size_t n) {
    return ::mi_realloc(ptr, n);
}


} // namespace utils
} // namespace xq


#endif // __XQ_UTILS_MEMORY_HPP__