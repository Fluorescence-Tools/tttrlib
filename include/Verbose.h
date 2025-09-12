#pragma once

#include <cstdlib>
#include <string>
#include <algorithm>
#include <cctype>

inline bool is_env_truthy(const char* v) {
    if (!v) return false;
    if (v[0] == '\0') return false;
    std::string s(v);
    // trim spaces
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char ch){ return !std::isspace(ch); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char ch){ return !std::isspace(ch); }).base(), s.end());
    if (s.empty()) return false;
    // consider "0", "false", "False", "no" as falsey
    std::string sl = s;
    std::transform(sl.begin(), sl.end(), sl.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
    if (sl == "0" || sl == "false" || sl == "no" || sl == "off") return false;
    return true;
}

inline const char* get_tttrlib_verbose_env() {
#ifdef _MSC_VER
    size_t len = 0;
    char* buf = nullptr;
    errno_t err = _dupenv_s(&buf, &len, "TTTRLIB_VERBOSE");
    static thread_local std::string val;
    val.clear();
    if (err || buf == nullptr) {
        return nullptr;
    }
    // _dupenv_s len includes terminating null if present
    if (len > 0) {
        val.assign(buf, (len && buf[len - 1] == '\0') ? (len - 1) : len);
    }
    free(buf);
    return val.c_str();
#else
    return std::getenv("TTTRLIB_VERBOSE");
#endif
}

inline bool is_verbose() {
    const char* env = get_tttrlib_verbose_env();
    return is_env_truthy(env);
}


