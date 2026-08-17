#pragma once
#include <cstdint>
typedef unsigned (*shim_thread_fn)(void*);
inline uintptr_t _beginthreadex(void*, unsigned, shim_thread_fn fn, void* arg, unsigned, unsigned* id) {
    if (id) *id = 1;
    fn(arg);          // synchronous: the workers only ever run to completion and are joined
    return 1;
}
inline void _endthread() {}
