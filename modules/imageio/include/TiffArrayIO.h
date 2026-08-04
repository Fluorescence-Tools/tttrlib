// SPDX-License-Identifier: BSD-3-Clause
//
// TiffIO - read and write 2D / 3D numeric arrays as TIFF files.
//
// Backed by a bundled, statically-linked libtiff (thirdparty/libtiff) so no
// additional runtime dependency is introduced. A TIFF file is treated as a
// stack of single-channel (grayscale) images: an (n_frames, height, width)
// array, where each frame is one TIFF directory/page. A plain 2D image is a
// stack with n_frames == 1.
//
// The public C++ interface is a single template pair, read_tiff<T>() /
// write_tiff<T>(), instantiated for T in {uint8, uint16, uint32, int32, float,
// double}. read_tiff<T> converts the file's native pixel type to T; the file's
// own type can be queried with tiff_info() / tiff_dtype() first. The Python /
// R / Java wrappers add a single auto-dispatching imread()/imwrite() on top.
//
// This module is compiled only when BUILD_TIFF is defined; otherwise the
// symbols still exist but throw "built without TIFF support".
#ifndef TTTRLIB_TIFFARRAYIO_H
#define TTTRLIB_TIFFARRAYIO_H

#include <string>
#include <cstdint>

namespace tttrlib {

/// Pixel data type stored in / read from a TIFF file.
enum class TiffDType {
    UInt8, UInt16, UInt32,
    Int8, Int16, Int32,
    Float32, Float64,
    Unknown
};

/// NumPy-style name ("uint16", "float32", ...) of a TiffDType.
std::string tiff_dtype_name(TiffDType dt);

/// Header description of a TIFF file seen as an (n_frames, height, width) stack.
struct TiffInfo {
    int n_frames = 0;              ///< number of pages / z-slices
    int height   = 0;              ///< rows (image length), equal across pages
    int width    = 0;              ///< columns (image width), equal across pages
    TiffDType dtype = TiffDType::Unknown; ///< native pixel type of the first page
};

/// Read only the header of \p path (no pixel decoding).
/// @throws std::runtime_error if the file cannot be opened / is inconsistent.
TiffInfo tiff_info(const std::string& path);

/// NumPy-style name ("uint16", "float32", ...) of the pixel type of \p path.
/// Convenience for language wrappers that dispatch on a dtype string.
std::string tiff_dtype(const std::string& path);

/// Read \p path into a freshly allocated, row-major (n_frames, height, width)
/// array, converting every pixel to T. A single-page file yields *dim1 == 1.
/// Ownership of *output transfers to the caller (free()); SWIG's
/// ARGOUTVIEWM_ARRAY3 typemap frees it from the wrapped languages.
/// All pages must share width, height and native pixel type.
template <typename T>
void read_tiff(const std::string& path, T** output, int* dim1, int* dim2, int* dim3);

/// Write a row-major (n_frames, height, width) array as a (possibly multi-page)
/// TIFF. \p compression is one of "none", "lzw" (default), "packbits" or
/// "deflate"/"zip" (the last requires libtiff built WITH_TIFF_ZLIB).
template <typename T>
void write_tiff(const std::string& path, T* data,
                int n_frames, int height, int width,
                const std::string& compression = "lzw");

} // namespace tttrlib

#endif // TTTRLIB_TIFFARRAYIO_H
