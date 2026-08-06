/* SPDX-License-Identifier: BSD-3-Clause */
/*!
 * \file tttrlib_example.c
 * \brief A complete tttrlib file-format plugin, in plain C.
 *
 * Build it, drop the result into a directory tttrlib searches, and the format
 * is there:
 *
 *     import tttrlib
 *     data = tttrlib.TTTR("measurement.exmpl")        # by content
 *     data = tttrlib.TTTR("measurement.exmpl", "EXAMPLE")   # or by name
 *     print(tttrlib.registry("plugin")["example"])
 *
 * No rebuild of tttrlib, no source, no change to the calling code. That is the
 * entire point of the exercise, and this file is the proof that the boundary is
 * really as narrow as it claims: plain C, one header, no linking against
 * tttrlib at all.
 *
 * \section ex_format The format
 *
 * Deliberately trivial, because the interesting part is the plumbing, not the
 * decoding. A 16-byte header and then fixed 16-byte records:
 *
 *     magic     8 bytes   "EXMPL001"
 *     count     uint64    number of records, little-endian
 *     record    uint64    macro time
 *               uint16    micro time
 *               int8      routing channel
 *               int8      event type (0 photon, 1 marker)
 *               4 bytes   padding
 *
 * \section ex_build Building it
 *
 *     cc -shared -fPIC -O2 -o tttrlib_example.so tttrlib_example.c \
 *        -I<tttrlib>/modules/plugin/include
 *
 * On macOS use `.dylib`, on Windows `.dll` and `cl /LD`. Note what is *not* in
 * that command line: any tttrlib library. A plugin resolves nothing from the
 * host at link time -- everything it may call arrives as a function pointer in
 * the host table -- which is why it survives being built by a different
 * compiler with a different runtime.
 */

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "tttrlib_plugin.h"

#define EXAMPLE_MAGIC "EXMPL001"
#define EXAMPLE_MAGIC_LEN 8
#define EXAMPLE_RECORD_LEN 16

static const tttrlib_host_v1* g_host = NULL;

/* ------------------------------------------------------------------ helpers */

static uint64_t read_u64_le(const unsigned char* p) {
    uint64_t v = 0;
    int i;
    for (i = 7; i >= 0; --i) v = (v << 8) | (uint64_t)p[i];
    return v;
}

static uint16_t read_u16_le(const unsigned char* p) {
    return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

/*! Everything one open file needs. The host never looks inside. */
typedef struct {
    FILE*    fp;
    uint64_t n_records;
    char     header_json[256];
} example_file;

/* ------------------------------------------------------------------ container */

/*!
 * Called speculatively, on files that are very likely not ours, so it has to be
 * cheap, quiet and certain. Eight magic bytes plus a length that agrees with
 * the file size is certain enough; the magic alone is not, because a truncated
 * file would then be accepted and fail later, during a read, where the error is
 * much harder to attribute.
 */
static int example_sniff(void* ctx, const char* path) {
    unsigned char head[16];
    long size;
    FILE* f;
    (void)ctx;

    f = fopen(path, "rb");
    if (f == NULL) return 0;
    if (fread(head, 1, sizeof(head), f) != sizeof(head)) { fclose(f); return 0; }
    if (memcmp(head, EXAMPLE_MAGIC, EXAMPLE_MAGIC_LEN) != 0) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    size = ftell(f);
    fclose(f);
    if (size < (long)sizeof(head)) return 0;
    return ((uint64_t)(size - (long)sizeof(head)) ==
            read_u64_le(head + EXAMPLE_MAGIC_LEN) * EXAMPLE_RECORD_LEN) ? 1 : 0;
}

static int example_open(void* ctx, const char* path, const char* params_json,
                        void** handle) {
    unsigned char head[16];
    example_file* self;
    (void)ctx;

    /* This format needs to be told nothing, so it declares no parameter schema
     * and should never be offered any. Saying so beats ignoring it: a caller
     * who passed parameters believes they did something. */
    if (params_json != NULL && params_json[0] != '\0' &&
        strcmp(params_json, "{}") != 0) {
        g_host->set_error("the EXAMPLE container takes no reader parameters");
        return TTTRLIB_INVALID;
    }

    self = (example_file*)calloc(1, sizeof(example_file));
    if (self == NULL) {
        g_host->set_error("out of memory");
        return TTTRLIB_ERROR;
    }
    self->fp = fopen(path, "rb");
    if (self->fp == NULL) {
        free(self);
        g_host->set_error("cannot open the file");
        return TTTRLIB_ERROR;
    }
    if (fread(head, 1, sizeof(head), self->fp) != sizeof(head) ||
        memcmp(head, EXAMPLE_MAGIC, EXAMPLE_MAGIC_LEN) != 0) {
        fclose(self->fp);
        free(self);
        g_host->set_error("not an EXAMPLE file (bad magic)");
        return TTTRLIB_ERROR;
    }
    self->n_records = read_u64_le(head + EXAMPLE_MAGIC_LEN);

    /* Metadata reaches tttrlib as the same tag JSON every built-in reader
     * produces, so a plugin format's header is not a second-class one. */
    snprintf(self->header_json, sizeof(self->header_json),
             "{\"tags\":["
             "{\"name\":\"MeasDesc_GlobalResolution\",\"value\":1e-9,\"idx\":-1,\"type\":536870920},"
             "{\"name\":\"MeasDesc_Resolution\",\"value\":1e-11,\"idx\":-1,\"type\":536870920},"
             "{\"name\":\"MeasDesc_NumberMicrotimes\",\"value\":4096,\"idx\":-1,\"type\":268435464}"
             "]}");

    *handle = self;
    return TTTRLIB_OK;
}

static int example_header_json(void* ctx, void* handle, const char** out) {
    example_file* self = (example_file*)handle;
    (void)ctx;
    *out = self->header_json;   /* owned by us, valid until close */
    return TTTRLIB_OK;
}

static int example_event_count_hint(void* ctx, void* handle, uint64_t* out) {
    example_file* self = (example_file*)handle;
    (void)ctx;
    *out = self->n_records;
    return TTTRLIB_OK;
}

/*!
 * The hot path, and the one place the ABI's third contract earns its keep: the
 * arrays belong to the host. We fill what fits and say how much that was; the
 * host calls again. Nothing is allocated here that the host will free.
 */
static int example_read(void* ctx, void* handle, tttrlib_events_v1* events) {
    example_file* self = (example_file*)handle;
    unsigned char record[EXAMPLE_RECORD_LEN];
    uint64_t written = 0;
    (void)ctx;

    while (written < events->capacity) {
        if (fread(record, 1, sizeof(record), self->fp) != sizeof(record)) break;
        if (events->macro_times)      events->macro_times[written]      = read_u64_le(record);
        if (events->micro_times)      events->micro_times[written]      = read_u16_le(record + 8);
        if (events->routing_channels) events->routing_channels[written] = (int8_t)record[10];
        if (events->event_types)      events->event_types[written]      = (int8_t)record[11];
        written += 1;
    }
    events->size = written;
    return TTTRLIB_OK;
}

static int example_close(void* ctx, void* handle) {
    example_file* self = (example_file*)handle;
    (void)ctx;
    if (self == NULL) return TTTRLIB_OK;
    if (self->fp != NULL) fclose(self->fp);
    free(self);
    return TTTRLIB_OK;
}

static tttrlib_container_v1 g_container = {
    sizeof(tttrlib_container_v1),
    "EXAMPLE",
    "Example plugin container",
    "A minimal file format, provided by a plugin to show that one can be.",
    "exmpl",
    NULL,          /* no reader parameters: the file says everything */
    1,             /* detectable: it has magic bytes worth trusting */
    example_sniff,
    example_open,
    example_header_json,
    example_event_count_hint,
    example_read,
    example_close,
    NULL           /* no per-container state beyond the open files */
};

/* ------------------------------------------------------------------ fit model
 *
 * A second capability from the same plugin, to show that one library can
 * contribute more than one kind of thing, and that a fit model reaches the
 * library's own machinery: `make_decay_fit("exp1", ...)` in C++,
 * `tttrlib.DecayFit2("exp1", ...)` from Python, and an entry in
 * `registry("fit")` next to the built-ins.
 *
 * The model is a single exponential with a constant offset -- deliberately the
 * simplest thing that is still a real fit, because what is being demonstrated
 * is the boundary, not the photophysics.
 *
 *     model[b] = amplitude * exp(-(b * dt) / tau) + offset
 *
 * and the objective is plain chi-square against the data over the fitted range.
 */

typedef struct {
    double dt_hint;    /* from setup[0], if the caller supplied one */
} example_fit;

static int fit_create(void* ctx, const double* setup, uint32_t n_setup,
                      const double* irf, uint32_t n_irf, void** instance) {
    example_fit* self;
    (void)ctx; (void)irf; (void)n_irf;

    self = (example_fit*)calloc(1, sizeof(example_fit));
    if (self == NULL) {
        g_host->set_error("out of memory");
        return TTTRLIB_ERROR;
    }
    /* Precomputing from setup and IRF is exactly why a model has a lifetime
     * rather than being one function; there is not much to precompute here. */
    self->dt_hint = (setup != NULL && n_setup > 0) ? setup[0] : 0.0;
    *instance = self;
    return TTTRLIB_OK;
}

static int fit_n_parameters(void* ctx, void* instance,
                            const tttrlib_fit_problem_v1* problem, int32_t* out) {
    (void)ctx; (void)instance; (void)problem;
    *out = 3;                      /* tau, amplitude, offset */
    return TTTRLIB_OK;
}

static int fit_n_results(void* ctx, void* instance,
                         const tttrlib_fit_problem_v1* problem, int32_t* out) {
    (void)ctx; (void)instance; (void)problem;
    *out = 1;                      /* chi2 */
    return TTTRLIB_OK;
}

static int fit_evaluate(void* ctx, void* instance, const double* x,
                        tttrlib_fit_problem_v1* problem, double* out) {
    const double tau = x[0], amplitude = x[1], offset = x[2];
    double chi2 = 0.0;
    int32_t c, b, start, stop;
    (void)ctx; (void)instance;

    if (problem->model == NULL || problem->n_bins <= 0) {
        g_host->set_error("the problem has no model buffer to fill");
        return TTTRLIB_INVALID;
    }
    if (!(tau > 0.0)) {
        g_host->set_error("tau must be positive");
        return TTTRLIB_INVALID;
    }

    start = problem->fit_start > 0 ? problem->fit_start : 0;
    stop = problem->fit_stop >= 0 ? problem->fit_stop : problem->n_bins;
    if (stop > problem->n_bins) stop = problem->n_bins;

    for (c = 0; c < problem->n_channels; ++c) {
        const int32_t base = c * problem->n_bins;
        for (b = 0; b < problem->n_bins; ++b) {
            const double t = (double)b * problem->dt;
            problem->model[base + b] = amplitude * exp(-t / tau) + offset;
        }
        if (problem->data == NULL) continue;
        for (b = start; b < stop; ++b) {
            const double m = problem->model[base + b];
            const double d = problem->data[base + b];
            const double r = d - m;
            /* Poisson-weighted, guarding the empty bins rather than dividing
             * by zero and returning a NaN the optimiser cannot act on. */
            chi2 += (m > 1e-12) ? (r * r) / m : r * r;
        }
    }
    *out = chi2;
    return TTTRLIB_OK;
}

static int fit_destroy(void* ctx, void* instance) {
    (void)ctx;
    free(instance);
    return TTTRLIB_OK;
}

static tttrlib_decay_fit_v1 g_fit = {
    sizeof(tttrlib_decay_fit_v1),
    "exp1_plugin",
    /* The schema is not decoration: it is what lets a caller pass parameters by
     * name instead of counting slots in a flat array, and the order here IS the
     * order of that array. */
    "{"
    "\"type\": \"object\","
    "\"required\": [\"tau\", \"amplitude\", \"offset\"],"
    "\"properties\": {"
    "\"tau\": {\"type\": \"number\", \"title\": \"Lifetime tau\", \"default\": 2.0,"
    " \"minimum\": 0.01, \"maximum\": 100.0, \"unit\": \"ns\","
    " \"description\": \"Decay constant of the single exponential.\"},"
    "\"amplitude\": {\"type\": \"number\", \"title\": \"Amplitude\", \"default\": 1.0,"
    " \"minimum\": 0.0, \"description\": \"Model value at time zero.\"},"
    "\"offset\": {\"type\": \"number\", \"title\": \"Constant offset\", \"default\": 0.0,"
    " \"minimum\": 0.0, \"description\": \"Flat background added to every bin.\"}"
    "}}",
    "Single exponential (plugin)",
    "One lifetime plus a constant offset, contributed by the example plugin.",
    fit_create,
    fit_n_parameters,
    fit_n_results,
    fit_evaluate,
    fit_destroy,
    NULL
};

/* ------------------------------------------------------------------ burst search
 *
 * The third capability, and the simplest table in the ABI: a burst search is
 * nearly a pure function -- arrival times in, index ranges out -- so it needs
 * no handle and no lifetime.
 *
 * This one is an interphoton-time threshold: a burst is a run of photons whose
 * consecutive gaps all fall below `max_gap` ticks, keeping runs of at least
 * `min_photons`. Crude next to the built-in searches, and deliberately so; what
 * is being shown is the boundary.
 */

static int example_burst_search(void* ctx,
                                const uint64_t* macro_times,
                                const int8_t* routing_channels,
                                uint64_t n,
                                double macro_time_resolution,
                                const char* params_json,
                                int64_t* out, uint64_t capacity, uint64_t* n_out) {
    double max_gap = 500.0, min_photons = 10.0;
    uint64_t found = 0, i = 0;
    (void)ctx; (void)routing_channels; (void)macro_time_resolution;

    /* A two-key JSON object, scanned rather than parsed: the plugin boundary is
     * C, and pulling in a JSON library to read two numbers would be a strange
     * dependency to impose on plugin authors. A real plugin with a larger
     * schema would of course use one -- its own, not the host's. */
    if (params_json != NULL) {
        const char* p = strstr(params_json, "\"max_gap\"");
        if (p != NULL && (p = strchr(p, ':')) != NULL) max_gap = atof(p + 1);
        p = strstr(params_json, "\"min_photons\"");
        if (p != NULL && (p = strchr(p, ':')) != NULL) min_photons = atof(p + 1);
    }

    while (i < n) {
        uint64_t j = i + 1;
        while (j < n && (double)(macro_times[j] - macro_times[j - 1]) <= max_gap) j += 1;
        if ((double)(j - i) >= min_photons) {
            /* Counted even when it does not fit: the host grows the buffer and
             * calls again, which is the contract that keeps a long measurement
             * from being silently truncated to the first N bursts. */
            if (found < capacity) {
                out[2 * found] = (int64_t)i;
                out[2 * found + 1] = (int64_t)j;
            }
            found += 1;
        }
        i = j;
    }
    *n_out = found;
    return TTTRLIB_OK;
}

static tttrlib_burst_search_v1 g_burst_search = {
    sizeof(tttrlib_burst_search_v1),
    "interphoton_plugin",
    "{"
    "\"type\": \"object\","
    "\"required\": [\"max_gap\", \"min_photons\"],"
    "\"properties\": {"
    "\"max_gap\": {\"type\": \"number\", \"title\": \"Maximum gap\", \"default\": 500.0,"
    " \"minimum\": 1.0, \"unit\": \"macro time ticks\","
    " \"description\": \"Largest gap between consecutive photons still inside one burst.\"},"
    "\"min_photons\": {\"type\": \"integer\", \"title\": \"Min photons\", \"default\": 10,"
    " \"minimum\": 1, \"description\": \"Runs shorter than this are discarded.\"}"
    "}}",
    "Interphoton time (plugin)",
    "A run of photons whose consecutive gaps stay below a threshold.",
    example_burst_search,
    NULL
};

/* ------------------------------------------------------------------ entry */

TTTRLIB_PLUGIN_EXPORT int tttrlib_plugin_init_v1(const tttrlib_host_v1* host,
                                                 tttrlib_plugin_info_v1* info) {
    /* Refuse a host older than the ABI we were built against, rather than
     * calling through a table that may be shorter than we think it is. */
    int status;
    if (host == NULL || host->struct_size < sizeof(tttrlib_host_v1)) {
        return TTTRLIB_UNSUPPORTED;
    }
    g_host = host;

    info->name = "example";
    info->version = "1.0.0";
    info->description = "A minimal file-format plugin, shipped as documentation.";

    host->log(TTTRLIB_LOG_DEBUG, "example plugin: registering the EXAMPLE container");
    status = host->register_container(&g_container);
    if (status != TTTRLIB_OK) return status;

    /* A second capability, from the same library. Guarded by struct_size:
     * register_decay_fit was appended to the host table after
     * register_container, so on an older host that field does not exist and
     * reading it would be a wild call. A plugin that needs it says so; one that
     * does not keeps working on both. */
    if (host->struct_size >= sizeof(tttrlib_host_v1) &&
        host->register_decay_fit != NULL) {
        status = host->register_decay_fit(&g_fit);
        if (status != TTTRLIB_OK) return status;
    }
    if (host->struct_size >= sizeof(tttrlib_host_v1) &&
        host->register_burst_search != NULL) {
        status = host->register_burst_search(&g_burst_search);
        if (status != TTTRLIB_OK) return status;
    }
    return TTTRLIB_OK;
}
