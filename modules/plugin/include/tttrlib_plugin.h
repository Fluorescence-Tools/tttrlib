/* SPDX-License-Identifier: BSD-3-Clause */
#ifndef TTTRLIB_PLUGIN_H
#define TTTRLIB_PLUGIN_H

/*!
 * \file tttrlib_plugin.h
 * \brief The tttrlib plugin ABI, version 1. The only file a plugin author needs.
 *
 * A plugin is a shared library named ``tttrlib_<name>.{so,dylib,dll}`` dropped
 * into a directory tttrlib searches. It adds a capability at run time: no
 * rebuild of tttrlib, no source, no change to the calling code.
 *
 * \section abi_c Why this boundary is plain C
 *
 * Because the whole point is that the plugin was built by somebody else, with a
 * different compiler, a different standard library and possibly a different C
 * runtime. Everything that makes C++ pleasant -- exceptions, templates,
 * ``std::string``, ``new``/``delete`` -- is precisely what does not survive that
 * gap. So the boundary is C structs and function pointers, and the C++
 * convenience lives in ``tttrlib_plugin.hpp``, on the plugin's side of the line,
 * where it is compiled by the plugin's own toolchain.
 *
 * \section abi_contracts The five contracts
 *
 * **1. Version negotiation is by symbol name.** The host looks up
 * ``tttrlib_plugin_init_v1``. A future ABI exports a *different* symbol, so a v1
 * plugin keeps working forever and a v2-only plugin on an old host gets a clean
 * "this plugin needs a newer tttrlib" rather than a crash. There is no version
 * integer to compare and get wrong.
 *
 * **2. Forward compatibility inside a version is by \ref struct_size.** Every
 * struct carries its own size as its first field. The reader checks the size
 * before touching a field that was added later. Fields may only ever be
 * appended -- never reordered, removed, or repurposed.
 *
 * **3. Event buffers are always host-allocated.** The plugin fills
 * ``[0, capacity)`` and reports how much it wrote. It never allocates memory
 * that tttrlib frees, or frees memory that tttrlib allocated. That single rule
 * makes the classic cross-runtime free mismatch structurally impossible on the
 * path that runs a billion times per file.
 *
 * **4. No exception crosses the boundary, in either direction.** Every host
 * callback is ``noexcept``. Every plugin entry point returns a status code;
 * ``tttrlib_plugin.hpp`` wraps C++ bodies in ``try``/``catch`` so an author
 * cannot get this wrong by accident. \ref TTTRLIB_UNSUPPORTED is deliberately
 * distinct from an error: a sniffer saying "not my format" is a normal answer.
 *
 * **5. Strings.** A string the host passes to the plugin is valid for that call
 * only. A string the plugin returns to the host is owned by the plugin and must
 * stay valid until the corresponding ``close``, or until shutdown for strings
 * that belong to the plugin as a whole. The host copies anything it keeps.
 *
 * \section abi_ex A minimal container plugin
 *
 * \code{.c}
 * static int my_sniff(void* ctx, const char* path) { ... }
 * static int my_open(void* ctx, const char* path, const char* params, void** h) { ... }
 * static int my_read(void* ctx, void* h, tttrlib_events_v1* ev) { ... }
 * static int my_close(void* ctx, void* h) { ... }
 *
 * static tttrlib_container_v1 kContainer = {
 *     sizeof(tttrlib_container_v1),
 *     "MYLAB", "My Lab time tagger", "mylab", NULL, 1,
 *     my_sniff, my_open, NULL, NULL, my_read, my_close, NULL
 * };
 *
 * TTTRLIB_PLUGIN_EXPORT int
 * tttrlib_plugin_init_v1(const tttrlib_host_v1* host, tttrlib_plugin_info_v1* info) {
 *     info->name = "mylab";
 *     info->version = "1.0.0";
 *     return host->register_container(&kContainer);
 * }
 * \endcode
 */

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*! \name Status codes
 * Returned by every plugin entry point and every host callback that can fail.
 * @{ */
#define TTTRLIB_OK           0   /*!< Success. */
#define TTTRLIB_ERROR        1   /*!< Failed; call set_error first to say why. */
#define TTTRLIB_UNSUPPORTED  2   /*!< Not an error: "this is not mine" / "I do not do that". */
#define TTTRLIB_INVALID      3   /*!< The caller passed something malformed. */
/*! @} */

/*! \name Log levels for tttrlib_host_v1::log
 * @{ */
#define TTTRLIB_LOG_DEBUG    0
#define TTTRLIB_LOG_INFO     1
#define TTTRLIB_LOG_WARNING  2
#define TTTRLIB_LOG_ERROR    3
/*! @} */

/*!
 * \brief The event arrays, host-allocated. See contract 3.
 *
 * tttrlib's whole data model is these four parallel arrays, so this struct is
 * the hot path and mirrors the internal decode loop's arguments exactly.
 *
 * The host sets every pointer, sets \ref capacity, and sets \ref size to 0. The
 * plugin writes up to \ref capacity events and sets \ref size to how many it
 * actually wrote. Writing past \ref capacity corrupts the host's heap; the
 * plugin must stop and return what fits, and the host will call again.
 *
 * A pointer may be NULL when the host does not want that column -- check before
 * writing. In practice all four are provided for a read.
 */
typedef struct tttrlib_events_v1 {
    uint32_t  struct_size;        /*!< sizeof(tttrlib_events_v1). */
    uint64_t* macro_times;        /*!< Coarse arrival time, in the container's own ticks. */
    uint16_t* micro_times;        /*!< TCSPC time within the excitation period, or 0. */
    int8_t*   routing_channels;   /*!< Detector/router number, or the marker number. */
    int8_t*   event_types;        /*!< 0 = photon, 1 = marker. */
    uint64_t  capacity;           /*!< Events the arrays can hold. Set by the host. */
    uint64_t  size;               /*!< Events written. Set by the plugin. */
} tttrlib_events_v1;

/*!
 * \brief A file format contributed by a plugin.
 *
 * Registering one makes it reachable everywhere a container name is: it can be
 * named in ``TTTR(filename, "MYLAB")``, it appears in the container name list
 * and in the ``file_container`` registry category, and -- if \ref detectable and
 * \ref sniff are set -- it takes part in content detection.
 *
 * The integer container id is assigned by the host at registration, from 1000
 * up, in load order. That makes it **session-local**: for a plugin format the
 * *name* is the stable identifier, and the registry says so via its ``stable``
 * field. Built-in formats keep 0-999 permanently.
 */
typedef struct tttrlib_container_v1 {
    uint32_t struct_size;         /*!< sizeof(tttrlib_container_v1). */

    /*! Stable identifier, e.g. "MYLAB". This is what a user passes and what the
     *  registry keys on. Registration is refused if the name is already taken --
     *  refused rather than shadowed, deliberately: a plugin that could silently
     *  replace the built-in PTU reader would be a supply-chain problem. */
    const char* name;

    const char* label;            /*!< Human-readable, e.g. "My Lab time tagger". */
    const char* summary;          /*!< One line for a UI that lists formats. May be NULL. */

    /*! Comma-separated extensions, lowercase, no dots: "mylab,mlb". May be NULL. */
    const char* extensions;

    /*! JSON Schema describing reader parameters, or NULL for none. Published as
     *  ``params_schema`` in the registry, so a caller discovers what this format
     *  must be told rather than having to be told. The same JSON object arrives
     *  back as the ``params_json`` argument to \ref open. */
    const char* params_schema;

    /*! Whether this format takes part in content-based detection. Set to 0 for a
     *  format that cannot be recognised from its bytes -- a bare stream of
     *  fixed-width words would otherwise match every file it is offered. */
    int detectable;

    /*! Recognise the format from the file's contents. Return 1 for yes, 0 for
     *  no. May be NULL, which means "accept on the extension alone" -- only
     *  correct for an extension nobody else could plausibly use.
     *  \note Must not throw, must not exit, and must be safe to call on a file
     *        that is not yours -- it is called speculatively. */
    int (*sniff)(void* ctx, const char* path);

    /*! Open \p path and produce an opaque handle. \p params_json is the caller's
     *  reader parameters as a JSON object, or NULL. Return \ref TTTRLIB_OK and
     *  set \p handle, or an error status after calling ``set_error``. */
    int (*open)(void* ctx, const char* path, const char* params_json, void** handle);

    /*! Metadata as a JSON object of tttrlib header tags, or NULL for none. The
     *  string is owned by the plugin and must outlive the call to \ref close.
     *  May be NULL if the format carries no metadata. */
    int (*header_json)(void* ctx, void* handle, const char** out);

    /*! How many events the file holds, if that is cheap to know. Lets the host
     *  allocate once instead of growing. An estimate is fine; the read loop does
     *  not depend on it. May be NULL. */
    int (*event_count_hint)(void* ctx, void* handle, uint64_t* out);

    /*! Decode the next batch into the host's buffers. Set ``events->size`` to
     *  the number written; set it to 0 to signal end of file. Called repeatedly
     *  until it reports 0 or fails. */
    int (*read)(void* ctx, void* handle, tttrlib_events_v1* events);

    /*! Release everything \ref open allocated, including any string handed back
     *  by \ref header_json. Always called exactly once per successful open. */
    int (*close)(void* ctx, void* handle);

    /*! Passed back to every callback above. The plugin's own state; the host
     *  never touches it. */
    void* ctx;
} tttrlib_container_v1;

/*!
 * \brief A fitting problem, as the plugin sees it.
 *
 * The host's own problem object is a set of ``std::vector``s; this is a view
 * over them, valid only for the duration of the call it was passed to. The
 * plugin must not keep the pointers, and must not resize anything -- the host
 * owns every buffer here, including \ref model, which is the one the plugin
 * writes into.
 *
 * Layout is channel-major: bin ``b`` of channel ``c`` is at ``c * n_bins + b``.
 * \ref irf and \ref background are either ``n_bins`` long, shared across
 * channels, or ``n_channels * n_bins``; \ref irf_is_per_channel says which,
 * because a polarisation-resolved setup may or may not have measured the two
 * detectors' responses separately.
 */
typedef struct tttrlib_fit_problem_v1 {
    uint32_t struct_size;         /*!< sizeof(tttrlib_fit_problem_v1). */

    const double* data;           /*!< Experimental histogram, n_channels * n_bins. */
    const double* irf;            /*!< Instrument response. See above for its length. */
    const double* background;     /*!< Background/scatter pattern, sized like irf. Not normalised. */

    /*! Fixed reference patterns -- a measured donor-only decay, an
     *  autofluorescence shape. ``patterns[i]`` is sized like \ref irf. Which
     *  index means what is the model's own business, and belongs in its
     *  registry entry. */
    const double* const* patterns;
    uint32_t n_patterns;

    /*! The model curve, sized and laid out exactly like \ref data. This is the
     *  plugin's output buffer: \ref tttrlib_decay_fit_v1::evaluate fills it. */
    double* model;

    int32_t  n_channels;
    int32_t  n_bins;
    double   dt;                  /*!< Width of one bin, in the model's own time unit. */

    /*! Construction inputs, flat, in the order the model's registry entry
     *  declares -- excitation period, g factor, detector mixing, and so on.
     *  Fixed for the fit; never optimised. */
    const double* setup;
    uint32_t n_setup;

    int32_t fit_start;            /*!< First bin of each channel in the objective. */
    int32_t fit_stop;             /*!< One past the last; negative means to the end. */
    int32_t irf_is_per_channel;   /*!< 1 if irf/background are n_channels * n_bins. */
} tttrlib_fit_problem_v1;

/*!
 * \brief A decay fit model contributed by a plugin.
 *
 * Registering one makes it constructible by name everywhere a fit is:
 * ``make_decay_fit("mymodel", setup, irf)`` in C++, ``DecayFit2("mymodel", …)``
 * from Python, and it appears in ``registry("fit")`` alongside the built-ins.
 *
 * The host builds one instance per fit through \ref create and destroys it
 * through \ref destroy, so a model may precompute from its setup and IRF and
 * keep that between evaluations -- which is the whole reason the interface has
 * a lifetime rather than being one function.
 */
typedef struct tttrlib_decay_fit_v1 {
    uint32_t struct_size;         /*!< sizeof(tttrlib_decay_fit_v1). */

    /*! What a caller passes to make_decay_fit. Refused if already taken. */
    const char* name;

    /*! JSON Schema of the optimised parameters, in the order they occupy the
     *  parameter array, published as ``params_schema`` in ``registry("fit")``.
     *  Without it the named-parameter helpers cannot describe this model and a
     *  caller is back to counting array slots, so it is required. */
    const char* params_schema;

    const char* label;            /*!< Human-readable. May be NULL. */
    const char* summary;          /*!< One line. May be NULL. */

    /*! Build an instance from its setup and IRF, both fixed for its lifetime.
     *  Return \ref TTTRLIB_OK and set \p instance. */
    int (*create)(void* ctx, const double* setup, uint32_t n_setup,
                  const double* irf, uint32_t n_irf, void** instance);

    /*! Number of optimised parameters, which may depend on the problem -- a
     *  multi-exponential model's count follows its number of components. */
    int (*n_parameters)(void* ctx, void* instance,
                        const tttrlib_fit_problem_v1* problem, int32_t* out);

    /*! Number of derived results the fit reports alongside the parameters. */
    int (*n_results)(void* ctx, void* instance,
                     const tttrlib_fit_problem_v1* problem, int32_t* out);

    /*! The objective at \p x: chi-square, or twice the negative log likelihood,
     *  whichever the model declares. Fills ``problem->model`` on the way. */
    int (*evaluate)(void* ctx, void* instance, const double* x,
                    tttrlib_fit_problem_v1* problem, double* out);

    /*! Release the instance. Always called exactly once per successful create. */
    int (*destroy)(void* ctx, void* instance);

    void* ctx;                    /*!< The plugin's own state. */
} tttrlib_decay_fit_v1;

/*!
 * \brief A burst search contributed by a plugin.
 *
 * A burst search reads a photon stream's arrival times and returns the index
 * ranges of the transits it found. That is the whole of it, which is why this
 * is the smallest of the three capability tables: no lifetime, no handle, one
 * call.
 *
 * Registering one makes it reachable by name --
 * ``tttr.burst_search_by_name("mysearch", **params)`` -- and puts it in the
 * ``burst_search`` registry category, so a UI builds its parameter form from
 * the schema without knowing the search exists.
 */
typedef struct tttrlib_burst_search_v1 {
    uint32_t struct_size;         /*!< sizeof(tttrlib_burst_search_v1). */

    /*! What a caller passes to burst_search_by_name. Refused if already taken. */
    const char* name;

    /*! JSON Schema of the parameters, published in the registry. Required: it
     *  is what lets a caller pass them by name and a form render itself. */
    const char* params_schema;

    const char* label;            /*!< Human-readable. May be NULL. */
    const char* summary;          /*!< One line. May be NULL. */

    /*!
     * \brief Find the bursts.
     *
     * \param macro_times   Arrival times, ascending, of the photons to search.
     *                      Read-only, valid for the call only.
     * \param routing_channels Their detector numbers, or NULL if the host has
     *                      none to offer. Same length as \p macro_times.
     * \param n             Number of photons.
     * \param macro_time_resolution Seconds per macro time tick, so a search can
     *                      work in seconds rather than in ticks. 0 if unknown.
     * \param params_json   The caller's parameters as a JSON object, or NULL.
     * \param out           Host-allocated, ``2 * capacity`` entries: the result
     *                      is flat ``[start, stop, start, stop, …]`` index pairs
     *                      into the photon arrays, stop exclusive.
     * \param capacity      Bursts \p out can hold.
     * \param n_out         Set to the number of bursts written. If the search
     *                      found more than \p capacity, set this to the total
     *                      it *would* have written and return
     *                      \ref TTTRLIB_OK -- the host will grow the buffer and
     *                      call again rather than silently truncating.
     */
    int (*search)(void* ctx,
                  const uint64_t* macro_times,
                  const int8_t* routing_channels,
                  uint64_t n,
                  double macro_time_resolution,
                  const char* params_json,
                  int64_t* out, uint64_t capacity, uint64_t* n_out);

    void* ctx;                    /*!< The plugin's own state. */
} tttrlib_burst_search_v1;

/*!
 * \brief What the host offers the plugin. Valid for the process lifetime.
 *
 * Handed to \ref tttrlib_plugin_init_v1. A plugin may keep the pointer.
 *
 * Every function here is ``noexcept`` and safe to call from any thread after
 * init returns, except the ``register_*`` functions, which may only be called
 * from inside init -- registration happens once, under the host's lock, and the
 * host rolls back everything a plugin registered if its init later fails.
 */
typedef struct tttrlib_host_v1 {
    uint32_t struct_size;         /*!< sizeof(tttrlib_host_v1). */
    uint32_t abi_version;         /*!< 1 for this header. */

    /*! tttrlib's own version string, e.g. "0.27.0". For a plugin that wants to
     *  refuse a host it was not built against. */
    const char* tttrlib_version;

    /*! Write a diagnostic. Goes wherever tttrlib's own diagnostics go, and is
     *  subject to the same verbosity setting, so a plugin does not have to
     *  invent its own logging or print to stderr unconditionally. */
    void (*log)(int level, const char* message);

    /*! Record why the current call failed. The host copies the string
     *  immediately. Call this *before* returning a non-OK status; a status with
     *  no message becomes a generic error the user cannot act on. */
    void (*set_error)(const char* message);

    /*! Contribute a file format. Only valid during init. Returns
     *  \ref TTTRLIB_OK, or \ref TTTRLIB_INVALID if the table is malformed or the
     *  name is already taken. */
    int (*register_container)(const tttrlib_container_v1* container);

    /*! Contribute a decay fit model. Only valid during init.
     *
     *  Appended after \ref register_container, which is what \ref struct_size is
     *  for: a plugin compiled against the earlier header has a smaller
     *  ``sizeof`` and never reads this field, and keeps working unchanged. A
     *  plugin that wants to use it must check ``struct_size`` first. */
    int (*register_decay_fit)(const tttrlib_decay_fit_v1* fit);

    /*! Contribute a burst search. Only valid during init. Appended after
     *  \ref register_decay_fit; the same \ref struct_size rule applies. */
    int (*register_burst_search)(const tttrlib_burst_search_v1* search);
} tttrlib_host_v1;

/*!
 * \brief What the plugin tells the host about itself.
 *
 * The host allocates this, zeroes it, sets \ref struct_size, and passes it to
 * init for the plugin to fill in.
 */
typedef struct tttrlib_plugin_info_v1 {
    uint32_t struct_size;         /*!< sizeof(tttrlib_plugin_info_v1). Set by the host. */

    /*! Short identifier, lowercase, e.g. "mylab". Should match the ``<name>`` in
     *  the ``tttrlib_<name>`` filename; if it does not, the filename wins for
     *  discovery and both are reported. Required. */
    const char* name;

    const char* version;          /*!< The plugin's own version, e.g. "1.2.0". Required. */
    const char* description;      /*!< One line. May be NULL. */

    /*! Called at process exit, if the host gets the chance. Optional, and a
     *  plugin should not depend on it: the host never unloads a plugin library
     *  (unloading after static constructors have run is a well-known crash
     *  source), so anything that must happen has already happened. */
    void (*shutdown)(void);
} tttrlib_plugin_info_v1;

/*!
 * \brief The entry point. Every plugin exports exactly this symbol.
 *
 * Called once, on the host's first use of a capability, with the host's lock
 * held. Fill in \p info and register whatever the plugin provides.
 *
 * Return \ref TTTRLIB_OK, or an error status after calling ``set_error``. On
 * failure the host rolls back every registration this plugin made and marks it
 * failed -- a broken plugin makes its own capability unavailable and changes
 * nothing else.
 */
#if defined(_WIN32)
#  define TTTRLIB_PLUGIN_EXPORT __declspec(dllexport)
#else
#  define TTTRLIB_PLUGIN_EXPORT __attribute__((visibility("default")))
#endif

typedef int (*tttrlib_plugin_init_v1_fn)(const tttrlib_host_v1* host,
                                         tttrlib_plugin_info_v1* info);

TTTRLIB_PLUGIN_EXPORT int tttrlib_plugin_init_v1(const tttrlib_host_v1* host,
                                                 tttrlib_plugin_info_v1* info);

#ifdef __cplusplus
}  /* extern "C" */
#endif

#endif /* TTTRLIB_PLUGIN_H */
