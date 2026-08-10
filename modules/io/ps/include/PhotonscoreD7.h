// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_PHOTONSCORED7_H
#define TTTRLIB_PHOTONSCORED7_H

#include <cstdint>
#include <map>
#include <string>
#include <vector>

/**
 * @file PhotonscoreD7.h
 * @brief Reader for Photonscore LINCam ".photons" files (the "D7" container).
 *
 * The ".photons" format is the position-sensitive photon-counting format
 * written by Photonscore's LINCam systems. A file is a stream of fixed size
 * pages (16384 bytes by default). Each page starts with a 2-byte block header
 * that is not part of the logical byte stream; with those headers removed the
 * stream is a protobuf @c Header, a run of @c Data blocks, a global @c Index
 * and an @c Epilogue.
 *
 * Each photon carries an (x, y) position (0 .. 2^PositionBits), a TCSPC micro
 * time @c dt (0 .. 2^TacBits) and a millisecond macro time marker @c ms. The
 * individual datasets are diff-encoded as a @c seed plus the cumulative sum of
 * zigzag-varint deltas.
 *
 * This decoder is a std-only port of the public, Apache-2.0 reference decoder
 * (github.com/photonscore/d7 and github.com/alex1075/photonsfile). It parses
 * the protobuf wire format by hand and needs no protobuf library. Only the
 * integer delta path (signed and unsigned integer datasets) is decoded; the
 * raw float, double and bytes fields are not used by LINCam FLIM files.
 */
namespace photonscore {

/// Signature at the very start of a ".photons" file.
inline constexpr const char* D7_MAGIC = "D7 Photons Data";

/// Default page (block) size in bytes.
constexpr uint32_t D7_DEFAULT_PAGE = 16384;

/// One entry of the file's table of contents.
struct D7Dataset {
    std::string name;   ///< e.g. "/photons/x", "/photons/dt", "/photons/ms"
    int type_code = -1; ///< 0-9: int8/16/32/64, uint8/16/32/64, float, double
};

/// Parsed ".photons" header (table of contents + versions).
struct D7Header {
    std::string magic;              ///< "D7 Photons Data"
    int version_major = 0;
    uint32_t index_step = 0;
    uint32_t page_size = D7_DEFAULT_PAGE;
    std::vector<D7Dataset> datasets; ///< in file order; index == dataset id
    size_t header_end = 0;           ///< byte offset just past the header message
};

/**
 * @brief Check whether a file is a Photonscore ".photons" (D7) file.
 *
 * Reads the first bytes and looks for the "D7 Photons Data" signature.
 * Does not throw; returns false for unreadable or non-matching files.
 */
bool is_photons_file(const std::string& path);

/**
 * @brief Read and parse the ".photons" header.
 * @throws std::runtime_error if the file cannot be read or the magic is absent.
 */
D7Header read_header(const std::string& path);

/**
 * @brief Read the file attributes (string key/value metadata).
 *
 * Keys include "/photons/PositionBits", "/photons/TacBits",
 * "/photons/TacChannel" (picoseconds per raw dt unit), "/photons/TacBias",
 * "/photons/TimerFrequency", "/photons/DetectorGuid" and "Created".
 */
std::map<std::string, std::string> read_attributes(const std::string& path);

/// True when the file records two TDCs ("/start/time" and "/stop/time").
bool has_dual_tdc(const std::string& path);

/**
 * @brief Decode the requested photon datasets.
 *
 * @param path    Path to the ".photons" file.
 * @param wanted  Dataset short names to decode (default: x, y, dt, ms).
 *                The "/photons/" prefix is added automatically. The special
 *                name "dt" resolves to "/stop/time" - "/start/time" on
 *                dual-TDC files.
 * @return Map short-name -> int64 values (only present datasets are returned).
 *         All decoded streams for a file share the same photon order.
 * @throws std::runtime_error if the header cannot be read.
 */
std::map<std::string, std::vector<int64_t>> read_photons(
        const std::string& path,
        const std::vector<std::string>& wanted =
                std::vector<std::string>({"x", "y", "dt", "ms"})
);

/// One dataset to write into a ".photons" file.
struct D7WriteDataset {
    std::string name;              ///< e.g. "/photons/x"
    int type_code;                 ///< D7 TypeCode (2 = int32, 3 = int64, ...)
    std::vector<int64_t> values;   ///< diff-encoded on write
};

/**
 * @brief Write a Photonscore ".photons" (D7) file.
 *
 * Produces a byte-exact D7 container (16384-byte pages with 2-byte block
 * headers, protobuf Header / Data blocks / Index / Epilogue). Datasets are
 * stored as a seed plus zigzag-varint deltas. The output is readable both by
 * this library and by the public photonsfile reference decoder.
 *
 * @param path       Output filename.
 * @param datasets   Datasets in file order (dataset id == position in vector).
 * @param attributes String key/value metadata (e.g. "/photons/TacBits").
 * @throws std::runtime_error if the file cannot be written.
 */
void write_photons(
        const std::string& path,
        const std::vector<D7WriteDataset>& datasets,
        const std::map<std::string, std::string>& attributes =
                std::map<std::string, std::string>()
);

} // namespace photonscore

#endif // TTTRLIB_PHOTONSCORED7_H
