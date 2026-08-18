// SPDX-License-Identifier: BSD-3-Clause
#include "PluginHost.h"

#include <algorithm>
#include <array>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include "TTTRFormat.h"
#include "Sha256.h"
#include "Verbose.h"

#if defined(_WIN32)
#  define NOMINMAX
#  include <windows.h>
#else
#  include <dlfcn.h>
#  include <sys/stat.h>
#endif

#ifndef TTTRLIB_VERSION_STRING
#  define TTTRLIB_VERSION_STRING "0.0.0"
#endif

namespace tttrlib {

namespace {

namespace fs = std::filesystem;

// sha256 of a plugin file: shared with the burst pipeline, see Sha256.h.
std::string sha256_of_file(const fs::path& p) {
    return util::sha256_file_hex(p.string());
}

// ---------------------------------------------------------------- environment

std::string env(const char* name) {
    const char* v = std::getenv(name);
    return v != nullptr ? std::string(v) : std::string();
}

std::vector<std::string> split_path_list(const std::string& s) {
#if defined(_WIN32)
    const char sep = ';';
#else
    const char sep = ':';
#endif
    std::vector<std::string> out;
    std::string current;
    for (char c : s) {
        if (c == sep) { if (!current.empty()) out.push_back(current); current.clear(); }
        else current.push_back(c);
    }
    if (!current.empty()) out.push_back(current);
    return out;
}

/*!
 * The directory this library itself lives in.
 *
 * The plugin directory is a sibling of the compiled library, wherever the
 * package ended up -- a wheel, a conda prefix, a build tree. Asking the loader
 * where it put us is the only way to know that which does not depend on Python
 * being involved: `inferTTTRFileType` is reached from C++ too.
 */
fs::path own_directory() {
#if defined(_WIN32)
    HMODULE module = nullptr;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                           GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCWSTR>(&own_directory), &module) == 0) {
        return {};
    }
    wchar_t path[MAX_PATH * 4];
    const DWORD n = GetModuleFileNameW(module, path, sizeof(path) / sizeof(path[0]));
    if (n == 0) return {};
    return fs::path(std::wstring(path, n)).parent_path();
#else
    Dl_info info{};
    if (dladdr(reinterpret_cast<const void*>(&own_directory), &info) == 0 ||
        info.dli_fname == nullptr) {
        return {};
    }
    std::error_code ec;
    const fs::path p = fs::absolute(fs::path(info.dli_fname), ec);
    return ec ? fs::path() : p.parent_path();
#endif
}

fs::path per_user_directory() {
#if defined(_WIN32)
    const std::string local = env("LOCALAPPDATA");
    if (local.empty()) return {};
    return fs::path(local) / "tttrlib" / "plugins";
#elif defined(__APPLE__)
    const std::string home = env("HOME");
    if (home.empty()) return {};
    return fs::path(home) / "Library" / "Application Support" / "tttrlib" / "plugins";
#else
    const std::string xdg = env("XDG_DATA_HOME");
    if (!xdg.empty()) return fs::path(xdg) / "tttrlib" / "plugins";
    const std::string home = env("HOME");
    if (home.empty()) return {};
    return fs::path(home) / ".local" / "share" / "tttrlib" / "plugins";
#endif
}

/*!
 * Is this directory safe to load binaries out of?
 *
 * The threat is not a clever attacker; it is a shared instrument drive where
 * somebody left a helpful-looking library next to the data. So: nothing
 * world-writable, ever. The directories that remain are inside the user's own
 * install or home, writable by exactly the account already running the
 * interpreter -- which could equally drop a `sitecustomize.py`, so refusing
 * those would be theatre.
 *
 * The temp directory is refused as well, but only when tttrlib went looking
 * there by itself (\p implicit). A path the user named in
 * ``TTTRLIB_PLUGIN_PATH`` is a deliberate act, and the whole point of the
 * variable is trying a plugin out before installing it -- which is exactly what
 * a scratch directory is for. Refusing it would leave the variable useless for
 * its main use and teach people to work around the check. Note that on Linux
 * the shared ``/tmp`` is world-writable and so is still refused by the rule
 * above; what this allows through is a per-user private temp directory.
 */
bool directory_is_safe(const fs::path& dir, bool implicit, std::string* why) {
    std::error_code ec;
    if (!fs::is_directory(dir, ec)) return false;

    const fs::path temp = implicit ? fs::temp_directory_path(ec) : fs::path();
    if (implicit && !ec && !temp.empty()) {
        const fs::path canonical = fs::weakly_canonical(dir, ec);
        const fs::path canonical_temp = fs::weakly_canonical(temp, ec);
        auto t = canonical_temp.begin();
        auto d = canonical.begin();
        bool under_temp = true;
        for (; t != canonical_temp.end(); ++t, ++d) {
            if (d == canonical.end() || *d != *t) { under_temp = false; break; }
        }
        if (under_temp && !canonical_temp.empty()) {
            if (why) *why = "refusing to load plugins from a temporary directory";
            return false;
        }
    }

#if !defined(_WIN32)
    struct stat st{};
    if (::stat(dir.string().c_str(), &st) == 0 && (st.st_mode & S_IWOTH) != 0) {
        if (why) *why = "refusing to load plugins from a world-writable directory";
        return false;
    }
#endif
    return true;
}

const char* library_suffix() {
#if defined(_WIN32)
    return ".dll";
#elif defined(__APPLE__)
    return ".dylib";
#else
    return ".so";
#endif
}

/// "tttrlib_mylab.dylib" -> "mylab"; anything else -> "".
std::string plugin_name_of(const fs::path& file) {
    const std::string stem = file.filename().string();
    const std::string prefix = "tttrlib_";
    const std::string suffix = library_suffix();
    if (stem.size() <= prefix.size() + suffix.size()) return {};
    if (stem.compare(0, prefix.size(), prefix) != 0) return {};
    if (stem.compare(stem.size() - suffix.size(), suffix.size(), suffix) != 0) return {};
    return stem.substr(prefix.size(), stem.size() - prefix.size() - suffix.size());
}

// ---------------------------------------------------------------- host state

/// Everything one plugin registered, so it can all be undone if its init fails.
struct Journal {
    std::vector<std::string> container_names;
    std::size_t decay_fits_before = 0;
    std::size_t burst_searches_before = 0;
    std::size_t operations_before = 0;
    std::size_t correlation_methods_before = 0;
    std::size_t decay_priors_before = 0;
};

struct HostState {
    std::vector<PluginRecord> records;
    /// container_type -> the plugin's table. Owned by the plugin's library,
    /// which is never unloaded, so the pointer stays valid for the process.
    std::vector<std::pair<int, const tttrlib_container_v1*>> containers;
    std::string error;        ///< set_error's landing pad, for the call in flight
    Journal* journal = nullptr;
    int next_container_type = 1000;

    /// Installed from above by the fitting layer; null until then.
    bool (*decay_fit_registrar)(const tttrlib_decay_fit_v1*) = nullptr;
    std::vector<const tttrlib_decay_fit_v1*> decay_fits;

    /// Burst searches need no registrar from above: a search is a pure function
    /// of the photon arrays, so the host can call it without help from any
    /// higher layer. That is why this one is simply a table.
    std::vector<const tttrlib_burst_search_v1*> burst_searches;

    // Generic operations. Same pattern: a plugin registers a
    // self-describing analysis step; the host stores the table and publishes
    // it via operations_json(). The pipeline dispatches by name.
    std::vector<const tttrlib_operation_v1*> operations;

    // Correlation kernels and prior kinds: plain tables too. The fcs and decay
    // layers look a name up here when their own table misses, so nothing is
    // pushed upward and nothing dangles when a failed plugin is rolled back.
    std::vector<const tttrlib_correlation_method_v1*> correlation_methods;
    std::vector<const tttrlib_decay_prior_v1*> decay_priors;
};

HostState& state() {
    static HostState s;
    return s;
}

std::mutex& state_mutex() {
    static std::mutex m;
    return m;
}

void host_log(int level, const char* message) noexcept {
    if (message == nullptr) return;
    if (level < TTTRLIB_LOG_WARNING && !is_verbose()) return;
    std::clog << "-- plugin: " << message << std::endl;
}

void host_set_error(const char* message) noexcept {
    state().error = message != nullptr ? message : "";
}

/*!
 * The sniffer trampoline.
 *
 * `FileFormat` holds a plain function pointer, which cannot carry the plugin's
 * context, so the format table grew a context-carrying variant and this is what
 * goes in it. Kept `noexcept` at the boundary: a plugin throwing through the
 * host is undefined, and sniffers are called speculatively on files that are
 * not theirs, which is exactly when a bad one misbehaves.
 */
bool plugin_sniff(void* ctx, const std::string& filename) noexcept {
    const auto* container = static_cast<const tttrlib_container_v1*>(ctx);
    if (container == nullptr || container->sniff == nullptr) return false;
    return container->sniff(container->ctx, filename.c_str()) == 1;
}

int host_register_container(const tttrlib_container_v1* c) noexcept {
    if (c == nullptr || c->struct_size < sizeof(tttrlib_container_v1) ||
        c->name == nullptr || c->name[0] == '\0' ||
        c->open == nullptr || c->read == nullptr || c->close == nullptr) {
        host_set_error("register_container: incomplete container table "
                       "(name, open, read and close are all required)");
        return TTTRLIB_INVALID;
    }

    FileFormat f;
    f.name = c->name;
    f.container_type = state().next_container_type;
    f.label = c->label != nullptr ? c->label : c->name;
    if (c->summary != nullptr) f.summary = c->summary;
    if (c->extensions != nullptr) {
        std::string current;
        for (const char* p = c->extensions; ; ++p) {
            if (*p == ',' || *p == '\0') {
                if (!current.empty()) f.extensions.push_back(current);
                current.clear();
                if (*p == '\0') break;
            } else {
                current.push_back(static_cast<char>(std::tolower(
                        static_cast<unsigned char>(*p))));
            }
        }
    }
    if (c->params_schema != nullptr) f.parameters_schema = c->params_schema;
    f.detectable = c->detectable != 0 && c->sniff != nullptr;
    f.sniff_with_context = &plugin_sniff;
    f.sniff_context = const_cast<tttrlib_container_v1*>(c);
    f.can_read = true;
    f.can_write = false;
    // A plugin's container id is handed out in load order, so it is only
    // meaningful within this process. The name is the stable identifier, and
    // `stable` is how a consumer knows not to persist the integer.
    f.stable = false;

    if (!IORegistry::add(f)) {
        const std::string taken = std::string("register_container: the name '") +
                                  c->name + "' is already taken";
        host_set_error(taken.c_str());
        return TTTRLIB_INVALID;
    }

    state().containers.emplace_back(f.container_type, c);
    if (state().journal != nullptr) state().journal->container_names.push_back(f.name);
    state().next_container_type += 1;
    return TTTRLIB_OK;
}

int host_register_decay_fit(const tttrlib_decay_fit_v1* f) noexcept {
    if (f == nullptr || f->struct_size < sizeof(tttrlib_decay_fit_v1) ||
        f->name == nullptr || f->name[0] == '\0' ||
        f->create == nullptr || f->evaluate == nullptr || f->destroy == nullptr ||
        f->n_parameters == nullptr) {
        host_set_error("register_decay_fit: incomplete model table (name, "
                       "params_schema, create, n_parameters, evaluate and "
                       "destroy are all required)");
        return TTTRLIB_INVALID;
    }
    if (f->params_schema == nullptr || f->params_schema[0] == '\0') {
        // Without it the named-parameter helpers cannot describe the model and
        // a caller is back to counting slots in a flat array, which is the
        // thing the registry exists to abolish.
        host_set_error("register_decay_fit: params_schema is required, so that "
                       "the model can be called by parameter name");
        return TTTRLIB_INVALID;
    }
    // Recorded first, wired up second -- and the two do not have to happen in
    // that order in time.
    //
    // Loading is triggered by whichever entry point is reached first, which may
    // be reading a file, and the fitting layer will not have installed its
    // registrar by then. Requiring it to be there would make a plugin's fit
    // model silently vanish depending on what the caller happened to do first.
    // So the table is always recorded, and set_decay_fit_registrar() replays
    // whatever accumulated before it arrived.
    state().decay_fits.push_back(f);
    if (state().decay_fit_registrar != nullptr &&
        !state().decay_fit_registrar(f)) {
        state().decay_fits.pop_back();
        const std::string taken = std::string("register_decay_fit: the name '") +
                                  f->name + "' is already taken";
        host_set_error(taken.c_str());
        return TTTRLIB_INVALID;
    }
    return TTTRLIB_OK;
}

int host_register_burst_search(const tttrlib_burst_search_v1* s) noexcept {
    if (s == nullptr || s->struct_size < sizeof(tttrlib_burst_search_v1) ||
        s->name == nullptr || s->name[0] == '\0' || s->search == nullptr) {
        host_set_error("register_burst_search: incomplete table (name, "
                       "params_schema and search are all required)");
        return TTTRLIB_INVALID;
    }
    if (s->params_schema == nullptr || s->params_schema[0] == '\0') {
        host_set_error("register_burst_search: params_schema is required, so "
                       "that the search can be called by parameter name");
        return TTTRLIB_INVALID;
    }
    for (const tttrlib_burst_search_v1* existing : state().burst_searches) {
        if (std::string(existing->name) == s->name) {
            const std::string taken =
                    std::string("register_burst_search: the name '") + s->name +
                    "' is already taken";
            host_set_error(taken.c_str());
            return TTTRLIB_INVALID;
        }
    }
    state().burst_searches.push_back(s);
    return TTTRLIB_OK;
}

int host_register_operation(const tttrlib_operation_v1* o) noexcept {
    if (o == nullptr || o->struct_size < sizeof(tttrlib_operation_v1) ||
        o->name == nullptr || o->name[0] == '\0' ||
        o->category == nullptr || o->category[0] == '\0' ||
        o->execute == nullptr) {
        host_set_error("register_operation: incomplete table (name, "
                       "category and execute are all required)");
        return TTTRLIB_INVALID;
    }
    if (o->settings_schema == nullptr || o->settings_schema[0] == '\0') {
        host_set_error("register_operation: settings_schema is required");
        return TTTRLIB_INVALID;
    }
    for (const tttrlib_operation_v1* existing : state().operations) {
        if (std::string(existing->name) == o->name) {
            const std::string taken =
                    std::string("register_operation: the name '") + o->name +
                    "' is already taken";
            host_set_error(taken.c_str());
            return TTTRLIB_INVALID;
        }
    }
    state().operations.push_back(o);
    return TTTRLIB_OK;
}

int host_register_correlation_method(const tttrlib_correlation_method_v1* m) noexcept {
    if (m == nullptr || m->struct_size < sizeof(tttrlib_correlation_method_v1) ||
        m->name == nullptr || m->name[0] == '\0' || m->correlate == nullptr) {
        host_set_error("register_correlation_method: incomplete table (name and "
                       "correlate are required)");
        return TTTRLIB_INVALID;
    }
    for (const tttrlib_correlation_method_v1* existing : state().correlation_methods) {
        if (std::string(existing->name) == m->name) {
            const std::string taken =
                    std::string("register_correlation_method: the name '") + m->name +
                    "' is already taken";
            host_set_error(taken.c_str());
            return TTTRLIB_INVALID;
        }
    }
    state().correlation_methods.push_back(m);
    return TTTRLIB_OK;
}

int host_register_decay_prior(const tttrlib_decay_prior_v1* p) noexcept {
    if (p == nullptr || p->struct_size < sizeof(tttrlib_decay_prior_v1) ||
        p->kind == nullptr || p->kind[0] == '\0' || p->create == nullptr ||
        p->lnpdf == nullptr) {
        host_set_error("register_decay_prior: incomplete table (kind, create "
                       "and lnpdf are required)");
        return TTTRLIB_INVALID;
    }
    for (const tttrlib_decay_prior_v1* existing : state().decay_priors) {
        if (std::string(existing->kind) == p->kind) {
            const std::string taken =
                    std::string("register_decay_prior: the kind '") + p->kind +
                    "' is already taken";
            host_set_error(taken.c_str());
            return TTTRLIB_INVALID;
        }
    }
    state().decay_priors.push_back(p);
    return TTTRLIB_OK;
}

const tttrlib_host_v1& host_table() {
    static const tttrlib_host_v1 host = {
        sizeof(tttrlib_host_v1),
        1,
        TTTRLIB_VERSION_STRING,
        &host_log,
        &host_set_error,
        &host_register_container,
        &host_register_decay_fit,
        &host_register_burst_search,
        &host_register_operation,
        &host_register_correlation_method,
        &host_register_decay_prior,
    };
    return host;
}

void roll_back(const Journal& journal) {
    // Fit models: the fitting layer's own table is left alone deliberately.
    // Un-registering a factory another thread may already be constructing
    // through is a worse failure than leaving one unreachable model behind, and
    // dropping it from this list is what keeps it out of the registry -- so the
    // model of a failed plugin is not offered to anybody.
    if (state().decay_fits.size() > journal.decay_fits_before) {
        state().decay_fits.resize(journal.decay_fits_before);
    }
    if (state().burst_searches.size() > journal.burst_searches_before) {
        state().burst_searches.resize(journal.burst_searches_before);
    }
    if (state().operations.size() > journal.operations_before) {
        state().operations.resize(journal.operations_before);
    }
    if (state().correlation_methods.size() > journal.correlation_methods_before) {
        state().correlation_methods.resize(journal.correlation_methods_before);
    }
    if (state().decay_priors.size() > journal.decay_priors_before) {
        state().decay_priors.resize(journal.decay_priors_before);
    }
    for (const std::string& name : journal.container_names) {
        const FileFormat* f = IORegistry::by_name(name);
        if (f != nullptr) {
            const int container_type = f->container_type;
            IORegistry::remove(name);
            auto& cs = state().containers;
            cs.erase(std::remove_if(cs.begin(), cs.end(),
                                    [&](const std::pair<int, const tttrlib_container_v1*>& e) {
                                        return e.first == container_type;
                                    }),
                     cs.end());
        }
    }
}

// ---------------------------------------------------------------- loading

struct Selection {
    bool enabled = true;
    bool pinned = false;
    std::vector<std::string> only;
};

Selection selection_from_env() {
    Selection s;
    const std::string v = env("TTTRLIB_PLUGINS");
    if (v.empty()) return s;
    if (v == "0" || v == "off" || v == "false" || v == "no") { s.enabled = false; return s; }
    const std::string prefix = "only:";
    if (v.compare(0, prefix.size(), prefix) == 0) {
        s.pinned = true;
        std::string current;
        for (std::size_t i = prefix.size(); i <= v.size(); ++i) {
            if (i == v.size() || v[i] == ',') {
                if (!current.empty()) s.only.push_back(current);
                current.clear();
            } else {
                current.push_back(v[i]);
            }
        }
    }
    return s;
}

/// Each directory, and whether tttrlib went looking there by itself. The flag
/// is what distinguishes "the user pointed at this" from "we found it".
std::vector<std::pair<fs::path, bool>> search_directories() {
    std::vector<std::pair<fs::path, bool>> dirs;
    for (const std::string& d : split_path_list(env("TTTRLIB_PLUGIN_PATH"))) {
        dirs.emplace_back(fs::path(d), false);   // named by the user
    }
    const fs::path own = own_directory();
    if (!own.empty()) dirs.emplace_back(own / "plugins", true);
    const fs::path user = per_user_directory();
    if (!user.empty()) dirs.emplace_back(user, true);
    return dirs;
}

fs::path quarantine_marker(const fs::path& dir, const std::string& name) {
    return dir / (".tttrlib-loading-" + name);
}

/// Open the library, call its entry point, keep or undo what it registered.
void load_one(PluginRecord& record) {
    const fs::path path(record.path);
    const fs::path marker = quarantine_marker(path.parent_path(), record.name);

    std::error_code ec;
    // Written before the library is opened and removed after it survives, so a
    // plugin that took the last process down with it is skipped next time
    // instead of making the interpreter unstartable. Best-effort: a read-only
    // plugin directory is a managed install, and gets no quarantine.
    const bool marked = [&] {
        std::FILE* f = std::fopen(marker.string().c_str(), "wb");
        if (f == nullptr) return false;
        std::fclose(f);
        return true;
    }();

    struct MarkerGuard {
        const fs::path& marker;
        bool marked;
        ~MarkerGuard() {
            if (marked) { std::error_code e; fs::remove(marker, e); }
        }
    } guard{marker, marked};

#if defined(_WIN32)
    const std::wstring wide = path.wstring();
    HMODULE handle = LoadLibraryExW(wide.c_str(), nullptr,
                                    LOAD_WITH_ALTERED_SEARCH_PATH);
    if (handle == nullptr) {
        record.status = PluginStatus::Failed;
        record.message = "LoadLibraryExW failed (error " +
                         std::to_string(static_cast<unsigned long>(GetLastError())) + ")";
        return;
    }
    auto init = reinterpret_cast<tttrlib_plugin_init_v1_fn>(
            reinterpret_cast<void*>(GetProcAddress(handle, "tttrlib_plugin_init_v1")));
#else
    // RTLD_NOW: a missing symbol becomes a diagnosis here rather than a crash
    // in the middle of reading somebody's data. RTLD_LOCAL: the plugin's
    // symbols never pre-empt tttrlib's own bundled statics.
    void* handle = dlopen(path.string().c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        record.status = PluginStatus::Failed;
        const char* e = dlerror();
        record.message = e != nullptr ? e : "dlopen failed";
        return;
    }
    auto init = reinterpret_cast<tttrlib_plugin_init_v1_fn>(dlsym(handle, "tttrlib_plugin_init_v1"));
#endif

    if (init == nullptr) {
        // Not a tttrlib plugin, or one built against a newer ABI that exports a
        // differently-named entry point. Either way it is not ours to call.
        record.status = PluginStatus::Failed;
        record.message = "no tttrlib_plugin_init_v1 symbol; "
                         "not a tttrlib plugin, or built for a newer ABI";
        return;
    }

    Journal journal;
    journal.decay_fits_before = state().decay_fits.size();
    journal.burst_searches_before = state().burst_searches.size();
    journal.operations_before = state().operations.size();
    journal.correlation_methods_before = state().correlation_methods.size();
    journal.decay_priors_before = state().decay_priors.size();
    state().journal = &journal;
    state().error.clear();

    tttrlib_plugin_info_v1 info{};
    info.struct_size = sizeof(info);

    int status = TTTRLIB_ERROR;
    try {
        status = init(&host_table(), &info);
    } catch (...) {
        // The ABI says no exception crosses. If one does anyway, the plugin is
        // broken in a way that makes everything else it claims untrustworthy.
        status = TTTRLIB_ERROR;
        state().error = "the plugin threw an exception through the C boundary";
    }
    state().journal = nullptr;

    if (status != TTTRLIB_OK) {
        roll_back(journal);
        record.status = PluginStatus::Failed;
        record.message = state().error.empty()
                             ? ("init returned status " + std::to_string(status))
                             : state().error;
        return;
    }
    if (info.name == nullptr || info.version == nullptr) {
        roll_back(journal);
        record.status = PluginStatus::Failed;
        record.message = "init succeeded but left name or version unset";
        return;
    }

    record.declared_name = info.name;
    record.version = info.version;
    if (info.description != nullptr) record.description = info.description;
    record.containers = journal.container_names;
    record.status = PluginStatus::Loaded;
}

void load_all() {
    const Selection selection = selection_from_env();
    if (!selection.enabled) return;

    std::vector<std::string> seen;   // first directory wins, by plugin name

    for (const auto& entry : search_directories()) {
        const fs::path& dir = entry.first;
        const bool implicit = entry.second;
        std::string why;
        if (!directory_is_safe(dir, implicit, &why)) {
            if (!why.empty() && is_verbose()) {
                std::clog << "-- plugin: " << dir.string() << ": " << why << std::endl;
            }
            continue;
        }

        // Filename order, case-folded, so two machines load in the same order
        // and a bug is reproducible instead of filesystem-dependent.
        std::vector<fs::path> candidates;
        std::error_code ec;
        for (const auto& entry : fs::directory_iterator(dir, ec)) {
            if (ec) break;
            if (!entry.is_regular_file(ec)) continue;
            if (plugin_name_of(entry.path()).empty()) continue;
            candidates.push_back(entry.path());
        }
        std::sort(candidates.begin(), candidates.end(),
                  [](const fs::path& a, const fs::path& b) {
                      std::string x = a.filename().string(), y = b.filename().string();
                      std::transform(x.begin(), x.end(), x.begin(), ::tolower);
                      std::transform(y.begin(), y.end(), y.begin(), ::tolower);
                      return x < y;
                  });

        for (const fs::path& file : candidates) {
            PluginRecord record;
            record.name = plugin_name_of(file);
            record.path = fs::absolute(file, ec).string();
            record.sha256 = sha256_of_file(file);

            if (std::find(seen.begin(), seen.end(), record.name) != seen.end()) {
                record.status = PluginStatus::Shadowed;
                record.message = "a plugin of this name was found earlier in the search path";
                state().records.push_back(record);
                continue;
            }
            seen.push_back(record.name);

            if (selection.pinned &&
                std::find(selection.only.begin(), selection.only.end(), record.name) ==
                        selection.only.end()) {
                record.status = PluginStatus::Disabled;
                record.message = "not listed in TTTRLIB_PLUGINS=only:...";
                state().records.push_back(record);
                continue;
            }

            if (fs::exists(quarantine_marker(dir, record.name), ec)) {
                record.status = PluginStatus::Quarantined;
                record.message = "a previous run did not survive loading this plugin; "
                                 "delete " + quarantine_marker(dir, record.name).string() +
                                 " to try again";
                state().records.push_back(record);
                continue;
            }

            load_one(record);
            state().records.push_back(record);
        }
    }
}

}  // namespace

void PluginHost::ensure_loaded() {
    static std::once_flag once;
    std::call_once(once, [] {
        std::lock_guard<std::mutex> guard(state_mutex());
        load_all();
    });
}

const std::vector<PluginRecord>& PluginHost::plugins() {
    ensure_loaded();
    return state().records;
}

const tttrlib_container_v1* PluginHost::container_for(int container_type) {
    ensure_loaded();
    for (const auto& entry : state().containers) {
        if (entry.first == container_type) return entry.second;
    }
    return nullptr;
}

std::string PluginHost::last_error() {
    return state().error;
}

void PluginHost::set_decay_fit_registrar(
        bool (*registrar)(const tttrlib_decay_fit_v1* fit)) {
    state().decay_fit_registrar = registrar;
    if (registrar == nullptr) return;
    // Replay whatever was recorded before the fitting layer showed up. Loading
    // may already have happened -- triggered by reading a file, say -- and the
    // models found then would otherwise be listed in the registry but not
    // constructible, which is the worst of both.
    for (const tttrlib_decay_fit_v1* f : state().decay_fits) registrar(f);
}

const std::vector<const tttrlib_decay_fit_v1*>& PluginHost::decay_fits() {
    ensure_loaded();
    return state().decay_fits;
}

const tttrlib_burst_search_v1* PluginHost::burst_search(const std::string& name) {
    ensure_loaded();
    for (const tttrlib_burst_search_v1* s : state().burst_searches) {
        if (name == s->name) return s;
    }
    return nullptr;
}

const std::vector<const tttrlib_burst_search_v1*>& PluginHost::burst_searches() {
    ensure_loaded();
    return state().burst_searches;
}

std::string PluginHost::burst_searches_json() {
    std::string out;
    for (const tttrlib_burst_search_v1* s : burst_searches()) {
        if (!out.empty()) out += ",\n";
        out += "  \"";
        out += s->name;
        out += "\": {\"name\": \"";
        out += s->name;
        out += "\", \"label\": \"";
        out += (s->label != nullptr ? s->label : s->name);
        out += "\", \"summary\": \"";
        out += (s->summary != nullptr ? s->summary
                                      : "A burst search provided by a plugin.");
        // No "method": a plugin search has no attribute on TTTR to call, and
        // its absence is exactly how a caller knows to dispatch by name.
        out += "\", \"provider\": \"plugin\", \"params_schema\": ";
        out += s->params_schema;
        out += "}";
    }
    return out;
}

// ── generic operations ────────────────────────────────────────────────

const tttrlib_operation_v1* PluginHost::operation(const std::string& name) {
    ensure_loaded();
    for (const tttrlib_operation_v1* o : state().operations) {
        if (std::string(o->name) == name) return o;
    }
    return nullptr;
}

const std::vector<const tttrlib_operation_v1*>& PluginHost::operations() {
    ensure_loaded();
    return state().operations;
}

// ── correlation methods / decay priors ───────────────────────────────

const tttrlib_correlation_method_v1* PluginHost::correlation_method(const std::string& name) {
    ensure_loaded();
    for (const tttrlib_correlation_method_v1* m : state().correlation_methods) {
        if (std::string(m->name) == name) return m;
    }
    return nullptr;
}

const std::vector<const tttrlib_correlation_method_v1*>& PluginHost::correlation_methods() {
    ensure_loaded();
    return state().correlation_methods;
}

const tttrlib_decay_prior_v1* PluginHost::decay_prior(const std::string& kind) {
    ensure_loaded();
    for (const tttrlib_decay_prior_v1* p : state().decay_priors) {
        if (std::string(p->kind) == kind) return p;
    }
    return nullptr;
}

const std::vector<const tttrlib_decay_prior_v1*>& PluginHost::decay_priors() {
    ensure_loaded();
    return state().decay_priors;
}

std::string PluginHost::operations_json() {
    std::string out;
    for (const tttrlib_operation_v1* o : operations()) {
        if (!out.empty()) out += ",\n";
        out += "  \"";
        out += o->name;
        out += "\": {\"name\": \"";
        out += o->name;
        out += "\", \"label\": \"";
        out += (o->label != nullptr ? o->label : o->name);
        out += "\", \"summary\": \"";
        out += (o->summary != nullptr ? o->summary
                                      : "A pipeline operation provided by a plugin.");
        out += "\", \"category\": \"";
        out += (o->category != nullptr ? o->category : "operation");
        out += "\", \"provider\": \"plugin\", \"can_replay\": ";
        out += (o->can_replay ? "true" : "false");
        if (o->settings_schema != nullptr) {
            out += ", \"settings_schema\": ";
            out += o->settings_schema;
        }
        if (o->inputs_json != nullptr) {
            out += ", \"inputs\": ";
            out += o->inputs_json;
        }
        if (o->outputs_json != nullptr) {
            out += ", \"outputs\": ";
            out += o->outputs_json;
        }
        if (o->row_grain != nullptr) {
            out += ", \"row_grain\": \"";
            out += o->row_grain;
            out += "\"";
        }
        out += "}";
    }
    return out;
}

std::string PluginHost::decay_fit_models_json() {
    // Built here rather than in the fitting layer because everything it needs
    // is in the C tables -- name, labels, and the schema the plugin already
    // wrote. That is what lets the registry, which must not depend on the
    // fitting stack, still publish a plugin's fit model.
    std::string out;
    for (const tttrlib_decay_fit_v1* f : decay_fits()) {
        if (!out.empty()) out += ",\n";
        out += "  \"";
        out += f->name;
        out += "\": {\"name\": \"";
        out += f->name;
        out += "\", \"label\": \"";
        out += (f->label != nullptr ? f->label : f->name);
        out += "\", \"summary\": \"";
        out += (f->summary != nullptr ? f->summary
                                      : "A decay fit model provided by a plugin.");
        out += "\", \"provider\": \"plugin\", \"params_schema\": ";
        out += f->params_schema;
        out += "}";
    }
    return out;
}

}  // namespace tttrlib
