// SPDX-License-Identifier: BSD-3-Clause
//
// TiffIO implementation - see include/TiffArrayIO.h.
//
// A TIFF is treated as an (n_frames, height, width) stack of single-channel
// images. Readers decode strip- and tile-based images and convert every native
// pixel type to the requested C type; writers emit baseline / BigTIFF with an
// optional lossless codec. Backed by the bundled static libtiff.
#include "TiffArrayIO.h"

#include <stdexcept>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>
#include <cstdint>

namespace tttrlib {

std::string tiff_dtype_name(TiffDType dt) {
    switch (dt) {
        case TiffDType::UInt8:   return "uint8";
        case TiffDType::UInt16:  return "uint16";
        case TiffDType::UInt32:  return "uint32";
        case TiffDType::Int8:    return "int8";
        case TiffDType::Int16:   return "int16";
        case TiffDType::Int32:   return "int32";
        case TiffDType::Float32: return "float32";
        case TiffDType::Float64: return "float64";
        default:                 return "unknown";
    }
}

} // namespace tttrlib

#ifdef BUILD_TIFF

#include <tiffio.h>

namespace {

// libtiff calls its error/warning handlers with printf-style messages. Route
// them through thread-local storage so we can attach the real reason to the
// std::runtime_error we throw, instead of spamming stderr.
thread_local std::string g_tiff_last_error;

void tiff_error_handler(const char* module, const char* fmt, va_list ap) {
    char buf[512];
    std::vsnprintf(buf, sizeof(buf), fmt, ap);
    g_tiff_last_error = module ? (std::string(module) + ": " + buf) : std::string(buf);
}
void tiff_warning_handler(const char*, const char*, va_list) { /* silenced */ }

struct TiffHandlerGuard {
    TIFFErrorHandler   prev_err;
    TIFFErrorHandler   prev_warn;
    TiffHandlerGuard() {
        g_tiff_last_error.clear();
        prev_err  = TIFFSetErrorHandler(tiff_error_handler);
        prev_warn = TIFFSetWarningHandler(tiff_warning_handler);
    }
    ~TiffHandlerGuard() {
        TIFFSetErrorHandler(prev_err);
        TIFFSetWarningHandler(prev_warn);
    }
};

// RAII wrapper around TIFF*.
struct TiffFile {
    TIFF* tif = nullptr;
    TiffFile(const std::string& path, const char* mode) {
        tif = TIFFOpen(path.c_str(), mode);
        if (!tif) {
            std::string why = g_tiff_last_error.empty() ? "cannot open file" : g_tiff_last_error;
            throw std::runtime_error("tiff: '" + path + "': " + why);
        }
    }
    ~TiffFile() { if (tif) TIFFClose(tif); }
    TiffFile(const TiffFile&) = delete;
    TiffFile& operator=(const TiffFile&) = delete;
};

// (sample format, bits-per-sample) as read from a directory.
struct NativeFormat {
    uint16_t sample_format = SAMPLEFORMAT_UINT;
    uint16_t bits = 0;
};

tttrlib::TiffDType to_dtype(const NativeFormat& nf) {
    using tttrlib::TiffDType;
    switch (nf.sample_format) {
        case SAMPLEFORMAT_UINT:
            if (nf.bits == 8)  return TiffDType::UInt8;
            if (nf.bits == 16) return TiffDType::UInt16;
            if (nf.bits == 32) return TiffDType::UInt32;
            break;
        case SAMPLEFORMAT_INT:
            if (nf.bits == 8)  return TiffDType::Int8;
            if (nf.bits == 16) return TiffDType::Int16;
            if (nf.bits == 32) return TiffDType::Int32;
            break;
        case SAMPLEFORMAT_IEEEFP:
            if (nf.bits == 32) return TiffDType::Float32;
            if (nf.bits == 64) return TiffDType::Float64;
            break;
    }
    return TiffDType::Unknown;
}

// Read the geometry / pixel format of the current directory and enforce the
// single-channel, supported-type contract used for array I/O.
void read_page_meta(TIFF* tif, uint32_t& w, uint32_t& h, NativeFormat& nf) {
    uint16_t spp = 1;
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLESPERPIXEL, &spp);
    if (spp != 1) {
        throw std::runtime_error(
            "tiff: only single-channel (grayscale) images are supported for array "
            "I/O, got " + std::to_string(spp) + " samples per pixel");
    }
    if (!TIFFGetField(tif, TIFFTAG_IMAGEWIDTH, &w) ||
        !TIFFGetField(tif, TIFFTAG_IMAGELENGTH, &h)) {
        throw std::runtime_error("tiff: missing image dimensions");
    }
    TIFFGetFieldDefaulted(tif, TIFFTAG_BITSPERSAMPLE, &nf.bits);
    nf.sample_format = SAMPLEFORMAT_UINT;
    TIFFGetFieldDefaulted(tif, TIFFTAG_SAMPLEFORMAT, &nf.sample_format);
    // Some files use SAMPLEFORMAT_VOID / 0; treat as unsigned integer.
    if (nf.sample_format != SAMPLEFORMAT_UINT &&
        nf.sample_format != SAMPLEFORMAT_INT &&
        nf.sample_format != SAMPLEFORMAT_IEEEFP) {
        nf.sample_format = SAMPLEFORMAT_UINT;
    }
    if (to_dtype(nf) == tttrlib::TiffDType::Unknown) {
        throw std::runtime_error(
            "tiff: unsupported pixel format (sampleformat=" +
            std::to_string(nf.sample_format) + ", bitspersample=" +
            std::to_string(nf.bits) + ")");
    }
}

// Cast a run of `n` native pixels (pointed to by `src`, described by `nf`) into
// the destination array of type T.
template <typename T>
void cast_native(const uint8_t* src, size_t n, const NativeFormat& nf, T* dst) {
    switch (nf.sample_format) {
        case SAMPLEFORMAT_UINT:
            if (nf.bits == 8)  { auto p = src;                                for (size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(p[i]); return; }
            if (nf.bits == 16) { auto p = reinterpret_cast<const uint16_t*>(src); for (size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(p[i]); return; }
            if (nf.bits == 32) { auto p = reinterpret_cast<const uint32_t*>(src); for (size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(p[i]); return; }
            break;
        case SAMPLEFORMAT_INT:
            if (nf.bits == 8)  { auto p = reinterpret_cast<const int8_t*>(src);  for (size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(p[i]); return; }
            if (nf.bits == 16) { auto p = reinterpret_cast<const int16_t*>(src); for (size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(p[i]); return; }
            if (nf.bits == 32) { auto p = reinterpret_cast<const int32_t*>(src); for (size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(p[i]); return; }
            break;
        case SAMPLEFORMAT_IEEEFP:
            if (nf.bits == 32) { auto p = reinterpret_cast<const float*>(src);  for (size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(p[i]); return; }
            if (nf.bits == 64) { auto p = reinterpret_cast<const double*>(src); for (size_t i = 0; i < n; ++i) dst[i] = static_cast<T>(p[i]); return; }
            break;
    }
    throw std::runtime_error("tiff: unsupported native pixel format while decoding");
}

// Decode the current directory into `native` (w*h native pixels, contiguous).
void decode_page_native(TIFF* tif, uint32_t w, uint32_t h,
                        const NativeFormat& nf, std::vector<uint8_t>& native) {
    const size_t bytes_per_px = nf.bits / 8;
    native.assign(static_cast<size_t>(w) * h * bytes_per_px, 0);

    if (TIFFIsTiled(tif)) {
        uint32_t tw = 0, th = 0;
        TIFFGetField(tif, TIFFTAG_TILEWIDTH, &tw);
        TIFFGetField(tif, TIFFTAG_TILELENGTH, &th);
        if (tw == 0 || th == 0) throw std::runtime_error("tiff: invalid tile geometry");
        std::vector<uint8_t> tile(TIFFTileSize(tif));
        for (uint32_t y0 = 0; y0 < h; y0 += th) {
            for (uint32_t x0 = 0; x0 < w; x0 += tw) {
                if (TIFFReadTile(tif, tile.data(), x0, y0, 0, 0) < 0) {
                    std::string why = g_tiff_last_error.empty() ? "tile read failed" : g_tiff_last_error;
                    throw std::runtime_error("tiff: " + why);
                }
                const uint32_t cw = (x0 + tw > w) ? (w - x0) : tw; // valid columns
                const uint32_t ch = (y0 + th > h) ? (h - y0) : th; // valid rows
                for (uint32_t ty = 0; ty < ch; ++ty) {
                    uint8_t* dst = native.data() +
                        (static_cast<size_t>(y0 + ty) * w + x0) * bytes_per_px;
                    const uint8_t* srow = tile.data() +
                        (static_cast<size_t>(ty) * tw) * bytes_per_px;
                    std::memcpy(dst, srow, static_cast<size_t>(cw) * bytes_per_px);
                }
            }
        }
    } else {
        const tmsize_t scanline = TIFFScanlineSize(tif);
        if (scanline < static_cast<tmsize_t>(static_cast<size_t>(w) * bytes_per_px)) {
            throw std::runtime_error("tiff: unexpected scanline size (planar/subsampled data?)");
        }
        for (uint32_t row = 0; row < h; ++row) {
            uint8_t* dst = native.data() + static_cast<size_t>(row) * w * bytes_per_px;
            if (TIFFReadScanline(tif, dst, row, 0) < 0) {
                std::string why = g_tiff_last_error.empty() ? "scanline read failed" : g_tiff_last_error;
                throw std::runtime_error("tiff: " + why);
            }
        }
    }
}

template <typename T>
void read_tiff_impl(const std::string& path, T** output,
                    int* dim1, int* dim2, int* dim3) {
    if (!output || !dim1 || !dim2 || !dim3)
        throw std::invalid_argument("tiff: null output pointer");
    TiffHandlerGuard hg;
    TiffFile f(path, "r");
    TIFF* tif = f.tif;

    uint32_t w0 = 0, h0 = 0;
    NativeFormat nf0;
    read_page_meta(tif, w0, h0, nf0);

    const uint16_t n = TIFFNumberOfDirectories(tif);
    const size_t page_px = static_cast<size_t>(w0) * h0;
    const size_t total_px = page_px * n;

    T* out = static_cast<T*>(std::malloc(total_px * sizeof(T)));
    if (!out) throw std::runtime_error("tiff: out of memory allocating output array");

    std::vector<uint8_t> native;
    try {
        for (uint16_t d = 0; d < n; ++d) {
            if (!TIFFSetDirectory(tif, d))
                throw std::runtime_error("tiff: cannot select page " + std::to_string(d));
            uint32_t w = 0, h = 0;
            NativeFormat nf;
            read_page_meta(tif, w, h, nf);
            if (w != w0 || h != h0 || nf.bits != nf0.bits ||
                nf.sample_format != nf0.sample_format) {
                throw std::runtime_error(
                    "tiff: inconsistent page geometry/format across the stack; "
                    "all pages must share width, height and pixel type");
            }
            decode_page_native(tif, w, h, nf, native);
            cast_native<T>(native.data(), page_px, nf, out + static_cast<size_t>(d) * page_px);
        }
    } catch (...) {
        std::free(out);
        throw;
    }

    *output = out;
    *dim1 = static_cast<int>(n);
    *dim2 = static_cast<int>(h0);
    *dim3 = static_cast<int>(w0);
}

uint16_t parse_compression(const std::string& c) {
    if (c.empty() || c == "none" || c == "raw")      return COMPRESSION_NONE;
    if (c == "lzw")                                   return COMPRESSION_LZW;
    if (c == "packbits")                              return COMPRESSION_PACKBITS;
    if (c == "deflate" || c == "zip" || c == "adobe_deflate") return COMPRESSION_ADOBE_DEFLATE;
    throw std::invalid_argument("tiff: unknown compression '" + c +
        "' (use none, lzw, packbits or deflate)");
}

// Map a C type to its TIFF SAMPLEFORMAT.
template <typename T> uint16_t sample_format_of();
template <> uint16_t sample_format_of<uint8_t>()  { return SAMPLEFORMAT_UINT; }
template <> uint16_t sample_format_of<uint16_t>() { return SAMPLEFORMAT_UINT; }
template <> uint16_t sample_format_of<uint32_t>() { return SAMPLEFORMAT_UINT; }
template <> uint16_t sample_format_of<int32_t>()  { return SAMPLEFORMAT_INT; }
template <> uint16_t sample_format_of<float>()    { return SAMPLEFORMAT_IEEEFP; }
template <> uint16_t sample_format_of<double>()   { return SAMPLEFORMAT_IEEEFP; }

template <typename T>
void write_tiff_impl(const std::string& path, T* data,
                     int n_frames, int height, int width,
                     const std::string& compression,
                     const std::string& description,
                     double x_resolution, double y_resolution) {
    const uint16_t sample_format = sample_format_of<T>();
    if (!data) throw std::invalid_argument("tiff: null input data");
    if (n_frames <= 0 || height <= 0 || width <= 0)
        throw std::invalid_argument("tiff: array dimensions must be positive");

    TiffHandlerGuard hg;
    const uint16_t comp = parse_compression(compression);
    if (!TIFFIsCODECConfigured(comp)) {
        throw std::runtime_error("tiff: compression '" + compression +
            "' is not available in this build (rebuild with WITH_TIFF_ZLIB for deflate)");
    }

    // Use BigTIFF when the raw pixel payload approaches the 4 GiB classic-TIFF
    // offset limit (leave head-room for headers/strip tables).
    const uint64_t payload =
        static_cast<uint64_t>(n_frames) * height * width * sizeof(T);
    const char* mode = (payload > (3ULL << 30)) ? "w8" : "w";

    TiffFile f(path, mode);
    TIFF* tif = f.tif;

    const uint16_t bits = static_cast<uint16_t>(sizeof(T) * 8);
    const bool multipage = n_frames > 1;
    std::vector<T> row(static_cast<size_t>(width)); // scratch: codecs may edit the scanline in place

    for (int d = 0; d < n_frames; ++d) {
        TIFFSetField(tif, TIFFTAG_IMAGEWIDTH,      static_cast<uint32_t>(width));
        TIFFSetField(tif, TIFFTAG_IMAGELENGTH,     static_cast<uint32_t>(height));
        TIFFSetField(tif, TIFFTAG_BITSPERSAMPLE,   bits);
        TIFFSetField(tif, TIFFTAG_SAMPLESPERPIXEL, static_cast<uint16_t>(1));
        TIFFSetField(tif, TIFFTAG_SAMPLEFORMAT,    sample_format);
        TIFFSetField(tif, TIFFTAG_PHOTOMETRIC,     PHOTOMETRIC_MINISBLACK);
        TIFFSetField(tif, TIFFTAG_PLANARCONFIG,    PLANARCONFIG_CONTIG);
        TIFFSetField(tif, TIFFTAG_ORIENTATION,     ORIENTATION_TOPLEFT);
        TIFFSetField(tif, TIFFTAG_COMPRESSION,     comp);
        TIFFSetField(tif, TIFFTAG_ROWSPERSTRIP,
                     TIFFDefaultStripSize(tif, static_cast<uint32_t>(height)));
        // Predictors improve LZW/Deflate ratios; horizontal for integers,
        // floating-point predictor for float samples.
        if (comp == COMPRESSION_LZW || comp == COMPRESSION_ADOBE_DEFLATE) {
            TIFFSetField(tif, TIFFTAG_PREDICTOR,
                sample_format == SAMPLEFORMAT_IEEEFP ? PREDICTOR_FLOATINGPOINT
                                                     : PREDICTOR_HORIZONTAL);
        }
        if (multipage) {
            TIFFSetField(tif, TIFFTAG_SUBFILETYPE, FILETYPE_PAGE);
            TIFFSetField(tif, TIFFTAG_PAGENUMBER,
                         static_cast<uint16_t>(d), static_cast<uint16_t>(n_frames));
        }
        // ImageJ reads the hyperstack layout from the first page only, and
        // repeating it on every page is what makes a stack open as N separate
        // images there.
        if (d == 0 && !description.empty()) {
            TIFFSetField(tif, TIFFTAG_IMAGEDESCRIPTION, description.c_str());
        }
        if (x_resolution > 0.0 && y_resolution > 0.0) {
            TIFFSetField(tif, TIFFTAG_XRESOLUTION, static_cast<float>(x_resolution));
            TIFFSetField(tif, TIFFTAG_YRESOLUTION, static_cast<float>(y_resolution));
            TIFFSetField(tif, TIFFTAG_RESOLUTIONUNIT, RESUNIT_NONE);
        }

        const T* frame = data + static_cast<size_t>(d) * height * width;
        for (int r = 0; r < height; ++r) {
            std::memcpy(row.data(), frame + static_cast<size_t>(r) * width,
                        static_cast<size_t>(width) * sizeof(T));
            if (TIFFWriteScanline(tif, row.data(), static_cast<uint32_t>(r), 0) < 0) {
                std::string why = g_tiff_last_error.empty() ? "scanline write failed" : g_tiff_last_error;
                throw std::runtime_error("tiff: " + why);
            }
        }
        if (!TIFFWriteDirectory(tif)) {
            std::string why = g_tiff_last_error.empty() ? "directory write failed" : g_tiff_last_error;
            throw std::runtime_error("tiff: " + why);
        }
    }
}

} // namespace

namespace tttrlib {

TiffInfo tiff_info(const std::string& path) {
    TiffHandlerGuard hg;
    TiffFile f(path, "r");
    uint32_t w = 0, h = 0;
    NativeFormat nf;
    read_page_meta(f.tif, w, h, nf);
    TiffInfo info;
    info.n_frames = static_cast<int>(TIFFNumberOfDirectories(f.tif));
    info.height   = static_cast<int>(h);
    info.width    = static_cast<int>(w);
    info.dtype    = to_dtype(nf);
    const char* desc = nullptr;
    if (TIFFGetField(f.tif, TIFFTAG_IMAGEDESCRIPTION, &desc) && desc) {
        info.description = desc;
    }
    float res = 0.0f;
    if (TIFFGetField(f.tif, TIFFTAG_XRESOLUTION, &res)) info.x_resolution = res;
    if (TIFFGetField(f.tif, TIFFTAG_YRESOLUTION, &res)) info.y_resolution = res;
    return info;
}

std::string tiff_dtype(const std::string& path) {
    return tiff_dtype_name(tiff_info(path).dtype);
}

template <typename T>
void read_tiff(const std::string& path, T** output, int* dim1, int* dim2, int* dim3) {
    read_tiff_impl<T>(path, output, dim1, dim2, dim3);
}

template <typename T>
void write_tiff(const std::string& path, T* data,
                int n_frames, int height, int width,
                const std::string& compression,
                const std::string& description,
                double x_resolution, double y_resolution) {
    write_tiff_impl<T>(path, data, n_frames, height, width, compression, description,
                       x_resolution, y_resolution);
}

// Explicit instantiations - one binding entry point per pixel type. The single
// public template above is what users call; these give the linker the six
// concrete symbols the SWIG %template wrappers bind to.
template void read_tiff<uint8_t> (const std::string&, uint8_t**,  int*, int*, int*);
template void read_tiff<uint16_t>(const std::string&, uint16_t**, int*, int*, int*);
template void read_tiff<uint32_t>(const std::string&, uint32_t**, int*, int*, int*);
template void read_tiff<int32_t> (const std::string&, int32_t**,  int*, int*, int*);
template void read_tiff<float>   (const std::string&, float**,    int*, int*, int*);
template void read_tiff<double>  (const std::string&, double**,   int*, int*, int*);

template void write_tiff<uint8_t> (const std::string&, uint8_t*,  int, int, int, const std::string&, const std::string&, double, double);
template void write_tiff<uint16_t>(const std::string&, uint16_t*, int, int, int, const std::string&, const std::string&, double, double);
template void write_tiff<uint32_t>(const std::string&, uint32_t*, int, int, int, const std::string&, const std::string&, double, double);
template void write_tiff<int32_t> (const std::string&, int32_t*,  int, int, int, const std::string&, const std::string&, double, double);
template void write_tiff<float>   (const std::string&, float*,    int, int, int, const std::string&, const std::string&, double, double);
template void write_tiff<double>  (const std::string&, double*,   int, int, int, const std::string&, const std::string&, double, double);

} // namespace tttrlib

#else // !BUILD_TIFF - symbols exist but report the disabled build.

namespace tttrlib {

static void tiff_disabled() {
    throw std::runtime_error("tttrlib was built without TIFF support (WITH_TIFF=OFF)");
}

TiffInfo tiff_info(const std::string&)        { tiff_disabled(); return {}; }
std::string tiff_dtype(const std::string&)    { tiff_disabled(); return {}; }

template <typename T>
void read_tiff(const std::string&, T**, int*, int*, int*) { tiff_disabled(); }
template <typename T>
void write_tiff(const std::string&, T*, int, int, int, const std::string&,
                const std::string&, double, double) { tiff_disabled(); }

template void read_tiff<uint8_t> (const std::string&, uint8_t**,  int*, int*, int*);
template void read_tiff<uint16_t>(const std::string&, uint16_t**, int*, int*, int*);
template void read_tiff<uint32_t>(const std::string&, uint32_t**, int*, int*, int*);
template void read_tiff<int32_t> (const std::string&, int32_t**,  int*, int*, int*);
template void read_tiff<float>   (const std::string&, float**,    int*, int*, int*);
template void read_tiff<double>  (const std::string&, double**,   int*, int*, int*);

template void write_tiff<uint8_t> (const std::string&, uint8_t*,  int, int, int, const std::string&, const std::string&, double, double);
template void write_tiff<uint16_t>(const std::string&, uint16_t*, int, int, int, const std::string&, const std::string&, double, double);
template void write_tiff<uint32_t>(const std::string&, uint32_t*, int, int, int, const std::string&, const std::string&, double, double);
template void write_tiff<int32_t> (const std::string&, int32_t*,  int, int, int, const std::string&, const std::string&, double, double);
template void write_tiff<float>   (const std::string&, float*,    int, int, int, const std::string&, const std::string&, double, double);
template void write_tiff<double>  (const std::string&, double*,   int, int, int, const std::string&, const std::string&, double, double);

} // namespace tttrlib

#endif // BUILD_TIFF
