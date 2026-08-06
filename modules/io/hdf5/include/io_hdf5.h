// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_IO_HDF5_H
#define TTTRLIB_IO_HDF5_H

/*!
 * \file io_hdf5.h
 * \brief Photon-HDF5 (``.h5``, ``.hdf5``), version 0.5.
 *
 * See https://photon-hdf5.org/ and the ``phconvert`` reference implementation.
 *
 * Unlike every other container tttrlib reads, a Photon-HDF5 file stores decoded
 * arrays rather than an instrument's record encoding, so there is nothing to
 * decode -- only datasets to find and read. What makes it awkward is the
 * opposite of the usual problem: the format is rich enough that its metadata
 * does not fit a fixed struct.
 *
 * \section hdf5_no_leak Why this interface says nothing about HDF5
 *
 * Neither HighFive nor ``hid_t`` appears below. That is deliberate and it is the
 * reason this module exists at all: for as long as the reader lived in `core`,
 * ``TTTRHeader.h`` had to forward-declare ``HighFive::Group`` for a single
 * private method, and every consumer of the photon-stream data model inherited a
 * dependency on an HDF5 toolchain it had no use for.
 *
 * Metadata therefore comes back as a flat list of \ref Hdf5Value -- group, name,
 * index, and one of three value kinds -- which the caller turns into whatever
 * it keeps metadata in. It is a slightly wider interface than handing over a
 * group handle, and it is the entire boundary.
 */

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace tttrlib {
namespace io {

/// True when tttrlib was built with Photon-HDF5 support (``BUILD_PHOTON_HDF``).
/// The functions below all fail cleanly when it is false.
bool photon_hdf5_available();

/*!
 * \brief One value out of a Photon-HDF5 metadata group.
 *
 * Photon-HDF5 metadata is a tree of small scalar and 1-D datasets whose names
 * and types are not fixed by the specification, so it is reported rather than
 * mapped: a caller keeps what it recognises.
 */
struct Hdf5Value {
    enum class Kind { Int, Float, String };

    Kind kind = Kind::Int;
    /// The value's IMMEDIATE parent group -- "setup", "identity", "sample",
    /// "provenance", "measurement_specs", "detectors_specs",
    /// "timestamps_specs", "nanotimes_specs" -- or empty for a root-level value.
    std::string group;
    std::string name;       ///< dataset name within the group
    int index = 0;          ///< position within a 1-D dataset; 0 for scalars
    long long i = 0;        ///< set when kind == Int
    double d = 0.0;         ///< set when kind == Float
    std::string s;          ///< set when kind == String
};

/*!
 * \brief The photon arrays of ``/photon_data``.
 *
 * The three datasets are independently optional in practice -- files in the
 * wild omit any of them -- so each is flagged rather than silently returned as
 * zeros. A caller that needs to distinguish "all detector 0" from "no detectors
 * recorded" can.
 */
struct PhotonHdf5Photons {
    std::vector<uint64_t> macro_times;       ///< /photon_data/timestamps
    std::vector<uint16_t> micro_times;       ///< /photon_data/nanotimes
    std::vector<int8_t>   routing_channels;  ///< /photon_data/detectors

    bool has_timestamps = false;
    bool has_nanotimes  = false;
    bool has_detectors  = false;

    std::size_t size() const { return macro_times.size(); }
};

/*!
 * \brief The mandatory fields a Photon-HDF5 file must carry, for writing.
 *
 * Defaults describe a single-spot lifetime measurement. A caller that read a
 * Photon-HDF5 file should pass back what that file said, so a round trip does
 * not quietly re-describe the instrument.
 */
/*!
 * \brief One metadata value on its way into a file.
 *
 * A tagged union rather than a variant so the header stays C++11-plain and
 * SWIG-free: \ref is_text picks which of the two payloads is live.
 */
struct PhotonHdf5Meta {
    std::string group;    ///< "sample", "provenance", "identity", "setup", "nanotimes_specs"
    std::string name;     ///< field name within that group
    bool is_text = true;
    std::string text;
    std::vector<double> numbers;   ///< one element for a scalar field
};

/*!
 * \brief Whether the specification defines ``group.name``, and its description.
 *
 * The description is not decoration: a Photon-HDF5 validator compares each
 * node's TITLE against the official text for that path, so a field written
 * without it -- or with a paraphrase -- makes the whole file invalid. Returns
 * nullptr for anything not in the specification, which is the writer's signal
 * to leave it out.
 */
const char* known_photon_hdf5_field(const std::string& group, const std::string& name);

struct PhotonHdf5Setup {
    double timestamps_unit = 0.0;   ///< seconds per macro time tick
    double tcspc_unit = 0.0;        ///< seconds per micro time bin
    int tcspc_num_bins = 0;

    int num_pixels = 1;
    int num_spots = 1;
    int num_spectral_ch = 1;
    int num_polarization_ch = 1;
    int num_split_ch = 1;

    bool modulated_excitation = false;
    bool lifetime = true;
    bool excitation_alternated = false;

    std::string description = "TTTR data written by tttrlib";
    std::string software = "tttrlib";
    std::string software_version;

    /*!
     * \brief Metadata to carry across, addressed as ``group.field``.
     *
     * Everything a Photon-HDF5 file says about the sample, the provenance of
     * the original recording and who made it -- ``sample.sample_name``,
     * ``provenance.filename``, ``identity.author``,
     * ``setup.excitation_wavelengths``. The reader already returns all of it;
     * without somewhere to put it on the way back out, a round trip through
     * this format quietly threw away half of what it was told.
     *
     * Only fields the specification defines are written, because a field
     * carries its description into the file and an invented one makes the file
     * fail validation. Anything else is skipped rather than guessed at; see
     * ``known_photon_hdf5_field()``.
     */
    std::vector<PhotonHdf5Meta> metadata;
};

/*!
 * \brief Read every metadata group in the file.
 *
 * Photon-HDF5 carries far more than ``/setup`` and ``/identity``: ``/sample``,
 * ``/provenance``, ``/photon_data/measurement_specs`` and its nested
 * ``detectors_specs``, plus root-level fields such as ``description`` and
 * ``acquisition_duration``. All of it is reported -- it is precisely the part a
 * caller cannot reconstruct from the photons.
 *
 * Photon arrays are told apart from metadata by size: a 1-D dataset longer than
 * 1024 elements is a measurement, not a description of one.
 *
 * Returns an empty list if the file has no metadata, or cannot be opened.
 */
std::vector<Hdf5Value> read_photon_hdf5_metadata(const std::string& filename);

/// Read the ``/photon_data`` arrays.
/// \throws std::runtime_error if the file cannot be opened.
PhotonHdf5Photons read_photon_hdf5_photons(const std::string& filename);

/*!
 * \brief Write a Photon-HDF5 v0.5 file.
 *
 * \param filename Output filename.
 * \param macro_times, routing_channels, micro_times, n_events The photon arrays.
 * \param setup Mandatory metadata; see \ref PhotonHdf5Setup.
 * \return false if the file could not be created, or HDF5 support is absent.
 */
bool write_photon_hdf5(const std::string& filename,
                       const uint64_t* macro_times,
                       const int8_t* routing_channels,
                       const uint16_t* micro_times,
                       std::size_t n_events,
                       const PhotonHdf5Setup& setup);

}  // namespace io
}  // namespace tttrlib

#endif  // TTTRLIB_IO_HDF5_H
