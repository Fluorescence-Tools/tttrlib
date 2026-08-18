// SPDX-License-Identifier: BSD-3-Clause
#include "BurstSearchDispatch.h"

// This file is the dispatch *mechanism* only. Which searches exist, and what
// they are, is declared in BurstSearchRegistry.cpp — so the per-search headers
// that used to be included here are not needed, and their absence is the point:
// nothing in this file knows the name of a single burst search.
#include "TTTR.h"
#include "PluginHost.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <mutex>
#include <stdexcept>
#include <unordered_map>

namespace tttrlib {

namespace {

struct Table {
    std::mutex m;
    std::vector<std::string> order;
    std::unordered_map<std::string, BurstSearchFn> fns;
};

Table& table() {
    static Table t;
    return t;
}

} // namespace

bool register_burst_search(const std::string& name, BurstSearchFn fn) {
    if (name.empty() || !fn) return false;
    Table& t = table();
    std::lock_guard<std::mutex> lock(t.m);
    if (t.fns.find(name) != t.fns.end()) return false;
    t.order.push_back(name);
    t.fns.emplace(name, std::move(fn));
    return true;
}

bool register_burst_search(const AlgorithmDescriptor& desc, BurstSearchFn fn) {
    // The dispatch key is the descriptor's registry key (`name`, or
    // `operation_type` when it has none) -- NOT its operation_type: the seven
    // searches all PERFORM `burst_selection` (mmfdb's term, what a .pto
    // records) and are told apart by their own names. Keying dispatch on the
    // operation type made six of them unreachable and the seventh answer for
    // all.
    const std::string& key = algorithm_key(desc);
    if (key.empty() || !fn) return false;

    // The descriptor is registered first because it is the one that can be
    // rejected on grounds the dispatch table knows nothing about (a duplicate
    // operation_type across every capability, not just this one). Registering
    // dispatch first and then failing here would leave a search runnable and
    // undescribed -- the state this overload exists to make unrepresentable.
    AlgorithmDescriptor d = desc;
    d.capability = "burst_search";
    if (!register_algorithm(d)) return false;

    return register_burst_search(key, std::move(fn));
}

const BurstSearchFn* find_burst_search(const std::string& name) {
    register_builtin_burst_searches();
    Table& t = table();
    {
        std::lock_guard<std::mutex> lock(t.m);
        auto it = t.fns.find(name);
        if (it != t.fns.end()) return &it->second;
    }

    // Not compiled in. A plugin may provide it -- and until this lookup existed
    // a plugin's search was reachable only through `burst_search_plugin()`, so
    // `burst_search(name)` did not fail on a plugin's name, it silently ran the
    // sliding window instead. The registry listed the search and the obvious
    // call did something else.
    //
    // Asked outside the lock: the plugin host loads libraries on demand and
    // takes its own locks.
    if (tttrlib::PluginHost::burst_search(name) == nullptr) return nullptr;

    BurstSearchFn fn = [name](TTTR& d, int L, int m, double T,
                              double alpha, double beta) {
        // The narrow entry point has exactly these five to offer, the same five
        // a built-in gets through this door. A plugin whose parameters do not
        // fit is called through `burst_search_plugin()` with its own JSON; it
        // reads what its schema declares out of this object and ignores the
        // rest. Built with a JSON writer rather than string concatenation
        // because `std::to_string` on a double is locale-dependent, and a comma
        // decimal separator would produce a parameter object no plugin can
        // parse -- on the machines where the locale happens to say so.
        nlohmann::json params;
        params["L"] = L;
        params["m"] = m;
        params["T"] = T;
        params["alpha"] = alpha;
        params["beta"] = beta;
        return d.burst_search_plugin(name, params.dump());
    };

    std::lock_guard<std::mutex> lock(t.m);
    auto it = t.fns.find(name);      // another thread may have won the race
    if (it == t.fns.end()) {
        t.order.push_back(name);
        it = t.fns.emplace(name, std::move(fn)).first;
    }
    // Stable: the table only grows, and an entry is never replaced, so the
    // pointer outlives the lock.
    return &it->second;
}

std::vector<std::string> burst_search_dispatch_names() {
    register_builtin_burst_searches();
    Table& t = table();
    std::lock_guard<std::mutex> lock(t.m);
    return t.order;
}

} // namespace tttrlib
