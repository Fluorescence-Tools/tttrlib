#include "Verbose.h"

bool is_verbose() {
    static const bool cached = []() {
        const char* env = get_tttrlib_verbose_env();
        return is_env_truthy(env);
    }();
    return cached;
}
