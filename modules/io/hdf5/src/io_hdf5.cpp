// SPDX-License-Identifier: BSD-3-Clause
#include "io_hdf5.h"

#include <algorithm>
#include <cstring>
#include <map>
#include <vector>
#include <ctime>
#include <iostream>
#include <stdexcept>

#ifdef BUILD_PHOTON_HDF
#include <hdf5.h>
#include <highfive/H5File.hpp>
#include <highfive/H5DataSet.hpp>
#include <highfive/H5DataSpace.hpp>
#include <highfive/H5Group.hpp>
#endif

namespace tttrlib {
namespace io {

#ifndef BUILD_PHOTON_HDF

bool photon_hdf5_available() { return false; }

std::vector<Hdf5Value> read_photon_hdf5_metadata(const std::string&) {
    std::cerr << "Not built with Photon HDF interface." << std::endl;
    return {};
}

PhotonHdf5Photons read_photon_hdf5_photons(const std::string&) {
    throw std::runtime_error("not built with Photon HDF interface");
}

bool write_photon_hdf5(const std::string&, const uint64_t*, const int8_t*,
                       const uint16_t*, std::size_t, const PhotonHdf5Setup&) {
    std::cerr << "Not built with Photon HDF interface." << std::endl;
    return false;
}

// Answerable without HDF5: it is a question about the specification, not about
// a file, and the header declares it either way.
const char* known_photon_hdf5_field(const std::string&, const std::string&) {
    return nullptr;
}

#else

bool photon_hdf5_available() { return true; }

namespace {

/*!
 * The largest 1-D dataset still treated as metadata.
 *
 * The recursive walk below cannot tell /setup/detection_wavelengths from
 * /photon_data/timestamps by name alone -- both are 1-D numeric datasets in a
 * group it is asked to descend into. It tells them apart by size, because no
 * metadata field in the specification is anywhere near this long and every
 * photon array is far beyond it. Without the bound, "read the metadata" would
 * pull twenty million timestamps in one element at a time.
 */
constexpr std::size_t kMaxMetadataElements = 1024;

/*!
 * Report every scalar and 1-D dataset in one metadata group.
 *
 * Photon-HDF5 does not fix the names or the types here, so nothing is looked
 * up: whatever is present is described and handed back. Datasets of two or more
 * dimensions are skipped -- the specification puts no metadata in them, and a
 * flat (group, name, index) address cannot describe one anyway.
 */
void collect_group(const HighFive::Group& group,
                   const std::string& group_name,
                   std::vector<Hdf5Value>& out) {
    for (const auto& obj_name : group.listObjectNames()) {
        if (group.getObjectType(obj_name) != HighFive::ObjectType::Dataset) continue;
        // One unreadable dataset must not cost the whole file's metadata.
        try {
        auto dataset = group.getDataSet(obj_name);
        {
            const auto d = dataset.getSpace().getDimensions();
            if (d.size() == 1 && d[0] > kMaxMetadataElements) continue;
        }
        auto datatype = dataset.getDataType();
        const auto dims = dataset.getSpace().getDimensions();
        if (!dims.empty() && dims.size() != 1) continue;

        const bool is_scalar = dims.empty() || dims[0] == 1;

        Hdf5Value v;
        v.group = group_name;
        v.name = obj_name;

        const bool is_integer =
                datatype == HighFive::AtomicType<int8_t>()   || datatype == HighFive::AtomicType<uint8_t>()  ||
                datatype == HighFive::AtomicType<int16_t>()  || datatype == HighFive::AtomicType<uint16_t>() ||
                datatype == HighFive::AtomicType<int32_t>()  || datatype == HighFive::AtomicType<uint32_t>() ||
                datatype == HighFive::AtomicType<int64_t>()  || datatype == HighFive::AtomicType<uint64_t>();
        const bool is_float =
                datatype == HighFive::AtomicType<float>() || datatype == HighFive::AtomicType<double>();

        if (is_integer) {
            v.kind = Hdf5Value::Kind::Int;
            if (is_scalar) {
                int value = 0;
                dataset.read(value);
                v.i = value;
                out.push_back(v);
            } else {
                std::vector<int> values;
                dataset.read(values);
                for (std::size_t idx = 0; idx < values.size(); ++idx) {
                    v.index = static_cast<int>(idx);
                    v.i = values[idx];
                    out.push_back(v);
                }
            }
        } else if (is_float) {
            v.kind = Hdf5Value::Kind::Float;
            if (is_scalar) {
                double value = 0.0;
                dataset.read(value);
                v.d = value;
                out.push_back(v);
            } else {
                std::vector<double> values;
                dataset.read(values);
                for (std::size_t idx = 0; idx < values.size(); ++idx) {
                    v.index = static_cast<int>(idx);
                    v.d = values[idx];
                    out.push_back(v);
                }
            }
        } else if (datatype.getClass() == HighFive::DataTypeClass::String) {
            v.kind = Hdf5Value::Kind::String;
            dataset.read(v.s);
            out.push_back(v);
        }
        // Anything else -- compound, enum, opaque, reference -- has no place in
        // the flat (kind, value) model and is skipped. Reading it as a string
        // was what the old four-group reader did; it never met one, because the
        // groups it looked at contain nothing but numbers and text. Walking the
        // whole file does, and one such dataset used to abort the entire read.
        } catch (const HighFive::Exception& err) {
            std::cerr << "Skipping " << group_name << "." << obj_name << ": "
                      << err.what() << std::endl;
        }
    }
}

/// Read one 1-D dataset into \p dst, sized from the file. Returns false when the
/// dataset is absent, leaving \p dst untouched.
template <typename T>
bool read_1d(hid_t file, const char* path, hid_t mem_type, std::vector<T>& dst) {
    if (H5Lexists(file, path, H5P_DEFAULT) <= 0) return false;
    const hid_t ds = H5Dopen(file, path, H5P_DEFAULT);
    if (ds < 0) return false;
    const hid_t space = H5Dget_space(ds);
    hsize_t dims[1] = {0};
    H5Sget_simple_extent_dims(space, dims, nullptr);
    dst.resize(static_cast<std::size_t>(dims[0]));
    if (dims[0] > 0) {
        H5Dread(ds, mem_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, dst.data());
    }
    H5Sclose(space);
    H5Dclose(ds);
    return true;
}

}  // namespace

namespace {

/*!
 * Walk every group in the file and report the metadata in all of them.
 *
 * Photon-HDF5 carries a good deal more than /setup and /identity -- /sample,
 * /provenance, /photon_data/measurement_specs and its nested /detectors_specs,
 * plus root-level fields like description and acquisition_duration. Reading a
 * fixed list of four groups threw the rest away, and it is exactly the part a
 * caller cannot reconstruct from the photons.
 *
 * A value is addressed by its IMMEDIATE parent group rather than by its full
 * path, so /photon_data/timestamps_specs/timestamps_unit stays
 * "timestamps_specs.timestamps_unit" as it has always been. Groups nest at most
 * three deep here and their names are distinct, so the short address stays
 * unambiguous while remaining what existing consumers already read.
 */
void walk(const HighFive::Group& group, const std::string& name,
          std::vector<Hdf5Value>& out, int depth) {
    if (depth > 8) return;   // the specification nests three deep; this is a cycle guard
    collect_group(group, name, out);
    for (const auto& obj_name : group.listObjectNames()) {
        if (group.getObjectType(obj_name) != HighFive::ObjectType::Group) continue;
        walk(group.getGroup(obj_name), obj_name, out, depth + 1);
    }
}

}  // namespace

std::vector<Hdf5Value> read_photon_hdf5_metadata(const std::string& filename) {
    std::vector<Hdf5Value> out;
    try {
        HighFive::File file(filename, HighFive::File::ReadOnly);

        // Root-level datasets (description, acquisition_duration, comment) have
        // no group, so they are addressed by name alone.
        collect_group(file.getGroup("/"), "", out);

        for (const auto& obj_name : file.getGroup("/").listObjectNames()) {
            if (file.getObjectType(obj_name) != HighFive::ObjectType::Group) continue;
            walk(file.getGroup(obj_name), obj_name, out, 1);
        }
    } catch (const HighFive::Exception& err) {
        std::cerr << "Error reading Photon-HDF5 metadata: " << err.what() << std::endl;
        return {};
    }
    return out;
}

PhotonHdf5Photons read_photon_hdf5_photons(const std::string& filename) {
    PhotonHdf5Photons out;

    const hid_t file = H5Fopen(filename.c_str(), H5F_ACC_RDONLY, H5P_DEFAULT);
    if (file < 0) throw std::runtime_error("cannot open " + filename);

    out.has_timestamps = read_1d(file, "/photon_data/timestamps",
                                 H5T_NATIVE_UINT64, out.macro_times);
    out.has_detectors = read_1d(file, "/photon_data/detectors",
                                H5T_NATIVE_INT8, out.routing_channels);
    out.has_nanotimes = read_1d(file, "/photon_data/nanotimes",
                                H5T_NATIVE_UINT16, out.micro_times);
    H5Fclose(file);

    // Timestamps are what defines how many photons there are. The other two are
    // padded to match rather than left short, so a caller can index all three
    // in one loop -- a file that records detectors for only some photons is
    // malformed, and truncating everything to the shortest array would hide it.
    const std::size_t n = out.macro_times.size();
    out.routing_channels.resize(n, 0);
    out.micro_times.resize(n, 0);
    return out;
}

namespace {

/*!
 * Attach the field's description as its TITLE attribute.
 *
 * Not decoration: the Photon-HDF5 validator compares TITLE against the
 * official description for that path and rejects the file when they differ, so
 * a file written without them is not a Photon-HDF5 file however correct its
 * numbers are. The strings below are therefore quoted verbatim from the
 * specification (photon-hdf5.org; the machine-readable copy lives in
 * phconvert's photon-hdf5_specs.json) rather than paraphrased -- a better
 * sentence here would be a worse file.
 */
void write_title(hid_t loc_or_dataset, const char* description) {
    if (description == nullptr) return;
    const hid_t str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(str_type, std::max<std::size_t>(1, std::strlen(description)));
    H5Tset_strpad(str_type, H5T_STR_NULLPAD);
    const hid_t space = H5Screate(H5S_SCALAR);
    const hid_t attr = H5Acreate2(loc_or_dataset, "TITLE", str_type, space,
                                  H5P_DEFAULT, H5P_DEFAULT);
    if (attr >= 0) {
        H5Awrite(attr, str_type, description);
        H5Aclose(attr);
    }
    H5Sclose(space);
    H5Tclose(str_type);
}

/// Create a group and describe it in one step, so a group cannot be added
/// without its description -- which is the mistake the validator catches.
hid_t create_group(hid_t loc, const char* name, const char* description) {
    const hid_t g = H5Gcreate2(loc, name, H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (g >= 0) write_title(g, description);
    return g;
}

void write_dataset(hid_t loc, const char* name, hid_t file_type,
                   hid_t mem_type, hsize_t n, const void* data,
                   const char* description = nullptr) {
    const hid_t space = H5Screate_simple(1, &n, nullptr);
    const hid_t ds = H5Dcreate2(loc, name, file_type, space,
                                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (n > 0) H5Dwrite(ds, mem_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
    write_title(ds, description);
    H5Dclose(ds);
    H5Sclose(space);
}

void write_scalar(hid_t loc, const char* name, hid_t file_type,
                  hid_t mem_type, const void* data,
                  const char* description = nullptr) {
    const hid_t space = H5Screate(H5S_SCALAR);
    const hid_t ds = H5Dcreate2(loc, name, file_type, space,
                                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(ds, mem_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
    write_title(ds, description);
    H5Dclose(ds);
    H5Sclose(space);
}

void write_scalar_int(hid_t loc, const char* name, int v,
                      const char* description = nullptr) {
    write_scalar(loc, name, H5T_STD_I32LE, H5T_NATIVE_INT32, &v, description);
}

void write_string(hid_t loc, const char* name, const std::string& s,
                  const char* description = nullptr) {
    const hid_t str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(str_type, std::max<std::size_t>(1, s.size()));
    H5Tset_strpad(str_type, H5T_STR_NULLPAD);
    const hid_t space = H5Screate(H5S_SCALAR);
    const hid_t ds = H5Dcreate2(loc, name, str_type, space,
                                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(ds, str_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, s.c_str());
    write_title(ds, description);
    H5Dclose(ds);
    H5Sclose(space);
    H5Tclose(str_type);
}

void write_root_attribute(hid_t file, const char* name, const std::string& s) {
    const hid_t str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(str_type, std::max<std::size_t>(1, s.size()));
    const hid_t space = H5Screate(H5S_SCALAR);
    const hid_t attr = H5Acreate2(file, name, str_type, space, H5P_DEFAULT, H5P_DEFAULT);
    H5Awrite(attr, str_type, s.c_str());
    H5Aclose(attr);
    H5Sclose(space);
    H5Tclose(str_type);
}


/*!
 * The metadata fields this writer is able to emit, with the description each
 * one must carry.
 *
 * Quoted verbatim from the Photon-HDF5 specification, because a validator
 * compares a node's TITLE against the official text for that path: a
 * paraphrase, however clearer, makes the file invalid. Fields absent from this
 * table are absent from the specification, and writing them would produce a
 * file that fails validation on a field nobody asked for -- so they are
 * dropped, which is the honest outcome for metadata the format has no place
 * for.
 */
enum class FieldKind { Text, Scalar, Array };

/*!
 * \p kind is declared, never inferred. A one-element array field written as a
 * scalar because it happened to hold one value is rejected by the validator --
 * /setup/detection_wavelengths on a single-colour measurement is an array of
 * length one, not a number.
 */
struct KnownField {
    const char* group;
    const char* name;
    FieldKind kind;
    const char* description;
};

const KnownField kKnownFields[] = {
    {"sample", "num_dyes", FieldKind::Scalar, "Number of different dyes present in the samples."},
    {"sample", "dye_names", FieldKind::Text,
     "String containing a comma-separated list of dye or fluorophore names."},
    {"sample", "buffer_name", FieldKind::Text, "A descriptive name for the buffer."},
    {"sample", "sample_name", FieldKind::Text, "A descriptive name for the sample."},

    {"provenance", "filename", FieldKind::Text,
     "File name of the original data file before conversion to Photon-HDF5."},
    {"provenance", "filename_full", FieldKind::Text,
     "File name (with full path) of the original data file before conversion to Photon-HDF5."},
    {"provenance", "creation_time", FieldKind::Text, "Creation time of the original data file."},
    {"provenance", "modification_time", FieldKind::Text,
     "Time of last modification of the original data file."},
    {"provenance", "software", FieldKind::Text, "Software used to save the original data file."},
    {"provenance", "software_version", FieldKind::Text,
     "Version of the software used to save the original data file."},

    {"identity", "author", FieldKind::Text, "Author of the current data file."},
    {"identity", "author_affiliation", FieldKind::Text,
     "Company or institution the author is affiliated with."},
    {"identity", "creator", FieldKind::Text, "Creator of the current Photon-HDF5 file."},
    {"identity", "creator_affiliation", FieldKind::Text,
     "Company or institution the creator is affiliated with."},
    {"identity", "url", FieldKind::Text, "URL that allow to download the Photon-HDF5 data file."},
    {"identity", "doi", FieldKind::Text, "Digital Object Identifier (DOI) for the Photon-HDF5 data file."},
    {"identity", "funding", FieldKind::Text,
     "A description of funding sources and/or grants used to produce the data."},
    {"identity", "license", FieldKind::Text, "The license under which the data is released."},
    // filename/filename_full describe *this* file at creation time, so they are
    // written from the output path rather than carried over from the source.

    {"setup", "excitation_wavelengths", FieldKind::Array,
     "List of excitation wavelengths (center wavelength if broad-band) in "
     "increasing order (unit: meter)."},
    {"setup", "excitation_cw", FieldKind::Array,
     "For each excitation source, this field indicates whether excitation is "
     "continuous wave (CW), True (i.e. 1), or pulsed, False (i.e. 0)."},
    {"setup", "laser_repetition_rates", FieldKind::Array,
     "Repetition rates in Hz for each laser. CW lasers have a value of 0."},
    {"setup", "excitation_polarizations", FieldKind::Array,
     "List of polarization angles (in degrees) for each excitation source."},
    {"setup", "excitation_input_powers", FieldKind::Array,
     "Excitation power in Watts for each excitation source. This is the "
     "excitation power entering the optical system."},
    {"setup", "detection_wavelengths", FieldKind::Array,
     "Reference wavelengths (units: meter) for each detected spectral band."},
    {"setup", "detection_polarizations", FieldKind::Array,
     "Polarization angles (in degrees) for each detected polarization."},

    {"nanotimes_specs", "tcspc_range", FieldKind::Scalar, "TCSPC full-scale range in seconds."},
};

const KnownField* find_known_field(const std::string& group, const std::string& name) {
    for (const KnownField& f : kKnownFields) {
        if (group == f.group && name == f.name) return &f;
    }
    return nullptr;
}

}  // namespace

const char* known_photon_hdf5_field(const std::string& group, const std::string& name) {
    const KnownField* f = find_known_field(group, name);
    return f != nullptr ? f->description : nullptr;
}

namespace {

/// Write one carried-over value, choosing the HDF5 shape from what it holds.
void write_meta(hid_t loc, const PhotonHdf5Meta& m, const KnownField& field) {
    switch (field.kind) {
        case FieldKind::Text:
            write_string(loc, m.name.c_str(), m.text, field.description);
            break;
        case FieldKind::Scalar:
            if (!m.numbers.empty()) {
                write_scalar(loc, m.name.c_str(), H5T_IEEE_F64LE, H5T_NATIVE_DOUBLE,
                             &m.numbers[0], field.description);
            }
            break;
        case FieldKind::Array:
            if (!m.numbers.empty()) {
                write_dataset(loc, m.name.c_str(), H5T_IEEE_F64LE, H5T_NATIVE_DOUBLE,
                              static_cast<hsize_t>(m.numbers.size()),
                              m.numbers.data(), field.description);
            }
            break;
    }
}

/*!
 * Write every carried-over value belonging to \p group into \p loc.
 *
 * \p loc may be -1, meaning "create the group only if something goes in it": an
 * empty /sample is worse than no /sample, since it claims the file describes a
 * sample it says nothing about.
 */
void write_meta_group(hid_t parent, const char* group, const char* group_path,
                      const char* group_description,
                      const std::vector<PhotonHdf5Meta>& metadata) {
    hid_t loc = -1;
    for (const PhotonHdf5Meta& m : metadata) {
        if (m.group != group) continue;
        const KnownField* field = find_known_field(m.group, m.name);
        if (field == nullptr) continue;
        if (loc < 0) loc = create_group(parent, group_path, group_description);
        if (loc < 0) return;
        write_meta(loc, m, *field);
    }
    if (loc >= 0) H5Gclose(loc);
}

}  // namespace

bool write_photon_hdf5(const std::string& filename,
                       const uint64_t* macro_times,
                       const int8_t* routing_channels,
                       const uint16_t* micro_times,
                       std::size_t n_events,
                       const PhotonHdf5Setup& setup) {
    // Layout follows the Photon-HDF5 specification (v0.5), see
    // https://photon-hdf5.org/ and the phconvert reference implementation.
    const hid_t file = H5Fcreate(filename.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, H5P_DEFAULT);
    if (file < 0) {
        std::cerr << "ERROR: Cannot create HDF5 file: " << filename << std::endl;
        return false;
    }

    const std::string format_name = "Photon-HDF5";
    const std::string format_version = "0.5";
    const std::string format_url = "http://photon-hdf5.org/";
    write_root_attribute(file, "format_name", format_name);
    write_root_attribute(file, "format_version", format_version);
    write_root_attribute(file, "format_url", format_url);
    // The root group needs its description too -- the validator walks every
    // node including "/", and a file that is right everywhere else still fails
    // on it.
    {
        const hid_t root = H5Gopen2(file, "/", H5P_DEFAULT);
        write_title(root, "A file format for photon-counting detector based "
                          "single-molecule spectroscopy experiments.");
        H5Gclose(root);
    }

    const hid_t g_photon_data = create_group(file, "/photon_data",
                                             "Group containing arrays of photon-data.");
    const hsize_t n = static_cast<hsize_t>(n_events);
    write_dataset(g_photon_data, "timestamps", H5T_STD_U64LE, H5T_NATIVE_UINT64, n,
                  macro_times,
                  "Array of photon timestamps. Units specified in timestamps_units "
                  "(defined in timestamps_specs/).");
    write_dataset(g_photon_data, "detectors", H5T_STD_I8LE, H5T_NATIVE_INT8, n,
                  routing_channels, "Array of pixel IDs for each timestamp.");
    write_dataset(g_photon_data, "nanotimes", H5T_STD_U16LE, H5T_NATIVE_UINT16, n,
                  micro_times,
                  "TCSPC photon arrival time (nanotimes). Units and other "
                  "specifications are in nanotimes_specs group.");

    // Specs groups, so resolutions and the micro time channel count survive.
    const hid_t g_ts_specs = create_group(g_photon_data, "timestamps_specs",
                                          "Specifications for timestamps.");
    write_scalar(g_ts_specs, "timestamps_unit", H5T_IEEE_F64LE,
                 H5T_NATIVE_DOUBLE, &setup.timestamps_unit,
                 "Value of 1-unit timestamp-increment in seconds.");
    H5Gclose(g_ts_specs);

    const hid_t g_nt_specs = create_group(g_photon_data, "nanotimes_specs",
                                          "Group for nanotime-specific data.");
    write_scalar(g_nt_specs, "tcspc_unit", H5T_IEEE_F64LE,
                 H5T_NATIVE_DOUBLE, &setup.tcspc_unit,
                 "Value of 1-unit nanotime-increment in seconds (TCSPC bin size).");
    write_scalar_int(g_nt_specs, "tcspc_num_bins", setup.tcspc_num_bins,
                     "Number of TCSPC bins.");
    for (const PhotonHdf5Meta& m : setup.metadata) {
        if (m.group != "nanotimes_specs") continue;
        const KnownField* field = find_known_field(m.group, m.name);
        if (field != nullptr) write_meta(g_nt_specs, m, *field);
    }
    H5Gclose(g_nt_specs);

    H5Gclose(g_photon_data);

    // ---- mandatory root fields ------------------------------------------
    write_string(file, "description", setup.description,
                 "A user-defined comment describing the data file.");
    double acquisition_duration = 0.0;
    if (n_events > 0) {
        acquisition_duration =
                static_cast<double>(macro_times[n_events - 1] - macro_times[0]) *
                setup.timestamps_unit;
    }
    write_scalar(file, "acquisition_duration", H5T_IEEE_F64LE,
                 H5T_NATIVE_DOUBLE, &acquisition_duration,
                 "Measurement duration in seconds.");

    // ---- /setup ----------------------------------------------------------
    const hid_t g_setup = create_group(file, "/setup",
                                       "Information about the experimental setup.");
    write_scalar_int(g_setup, "num_pixels", setup.num_pixels,
                     "Total number of detector pixels.");
    write_scalar_int(g_setup, "num_spots", setup.num_spots,
                     "Number of excitation (or detection) \"spots\" in the sample.");
    write_scalar_int(g_setup, "num_spectral_ch", setup.num_spectral_ch,
                     "Number of distinct spectral bands which are acquired.");
    write_scalar_int(g_setup, "num_polarization_ch", setup.num_polarization_ch,
                     "Number of distinct polarization states which are acquired.");
    write_scalar_int(g_setup, "num_split_ch", setup.num_split_ch,
                     "Number of distinct detection channels detecting the same "
                     "spectral band and polarization. This value is > 1 when using "
                     "a non-polarizing beam splitter.");
    const int8_t modulated = setup.modulated_excitation ? 1 : 0;
    const int8_t lifetime = setup.lifetime ? 1 : 0;
    const int8_t alternated = setup.excitation_alternated ? 1 : 0;
    write_scalar(g_setup, "modulated_excitation", H5T_STD_I8LE, H5T_NATIVE_INT8, &modulated,
                 "True (i.e. 1) if there is any form of excitation modulation of "
                 "excitation wavelength (as in us-ALEX or PAX) or polarization. This "
                 "field is also True for pulse-interleaved excitation (PIE) or "
                 "ns-ALEX measurements.");
    write_scalar(g_setup, "lifetime", H5T_STD_I8LE, H5T_NATIVE_INT8, &lifetime,
                 "True (i.e. 1) if the measurement includes a nanotimes array of "
                 "photon arrival times with respect to a laser pulse (as in TCSPC "
                 "measurements).");
    // An array rather than a scalar, deliberately: the specification types this
    // one per excitation source.
    write_dataset(g_setup, "excitation_alternated", H5T_STD_I8LE, H5T_NATIVE_INT8, 1,
                  &alternated,
                  "New in version 0.5. Indicates whether each excitation source is "
                  "alternated (True, or 1) or not alternated (False, or 0).");

    // ---- /setup/detectors -------------------------------------------------
    //
    // Mandatory since v0.5, and the one piece this writer used to omit: every
    // value appearing in /photon_data/detectors has to be declared here, which
    // is also how a reader learns that an ID belongs to something other than a
    // photon detector -- a monitor channel, or a marker the acquisition
    // hardware saved.
    //
    // Derived from the data rather than from the caller's header. The counts
    // have to match what was actually written or the file fails validation, and
    // a header carried over from a source file describes that file's detectors,
    // not this one's.
    {
        std::map<int8_t, uint64_t> counts;
        for (std::size_t i = 0; i < n_events; ++i) counts[routing_channels[i]] += 1;
        std::vector<int8_t> ids;
        std::vector<int64_t> id_counts;
        ids.reserve(counts.size());
        id_counts.reserve(counts.size());
        for (const auto& kv : counts) {
            ids.push_back(kv.first);
            id_counts.push_back(static_cast<int64_t>(kv.second));
        }
        const hid_t g_det = create_group(g_setup, "detectors",
                                         "Metadata relative to each detector's pixel. "
                                         "Each field is an array with size equal to the "
                                         "number of the detectors.");
        const hsize_t n_det = static_cast<hsize_t>(ids.size());
        write_dataset(g_det, "id", H5T_STD_I8LE, H5T_NATIVE_INT8, n_det, ids.data(),
                      "Detector IDs as they appear on /photon_data/detectors.");
        write_dataset(g_det, "id_hardware", H5T_STD_I8LE, H5T_NATIVE_INT8, n_det, ids.data(),
                      "Original IDs assigned by the acquisition hardware to each detector.");
        write_dataset(g_det, "counts", H5T_STD_I64LE, H5T_NATIVE_INT64, n_det,
                      id_counts.data(),
                      "Total number of counts detected by each detector.");
        H5Gclose(g_det);
    }
    // Carried-over /setup arrays: excitation wavelengths, laser repetition
    // rates and the rest. Written here so they sit in the same group the
    // reader found them in.
    for (const PhotonHdf5Meta& m : setup.metadata) {
        if (m.group != "setup") continue;
        const KnownField* field = find_known_field(m.group, m.name);
        if (field != nullptr) write_meta(g_setup, m, *field);
    }
    H5Gclose(g_setup);

    // ---- /sample and /provenance -----------------------------------------
    //
    // Neither is mandatory, and neither is invented: each group is created only
    // if the caller actually carried something for it. An empty /sample is
    // worse than none, because it claims the file describes a sample and then
    // says nothing about it.
    write_meta_group(file, "sample", "/sample",
                     "Information about the measured sample.", setup.metadata);
    write_meta_group(file, "provenance", "/provenance",
                     "Information about the original data file.", setup.metadata);

    // ---- /identity -------------------------------------------------------
    const hid_t g_identity = create_group(file, "/identity",
                                          "Information about the Photon-HDF5 data file.");
    write_string(g_identity, "format_name", format_name, "Name of the file format.");
    write_string(g_identity, "format_version", format_version,
                 "Version for the Photon-HDF5 format.");
    write_string(g_identity, "format_url", format_url,
                 "Official URL for the Photon-HDF5 format.");
    write_string(g_identity, "software", setup.software,
                 "Name of the software used to create the current Photon-HDF5 file.");
    write_string(g_identity, "software_version", setup.software_version,
                 "Version of the software used to create current the Photon-HDF5 file.");
    const std::time_t now = std::time(nullptr);
    char time_buffer[32];
    std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    write_string(g_identity, "creation_time", time_buffer,
                 "Creation time of the current Photon-HDF5 file.");
    // Author, funding, licence and the rest of what a published file should
    // carry. filename/filename_full are deliberately not among them: they
    // describe *this* file at creation time, so copying a source file's values
    // would be a false statement about the file being written.
    for (const PhotonHdf5Meta& m : setup.metadata) {
        if (m.group != "identity") continue;
        const KnownField* field = find_known_field(m.group, m.name);
        if (field != nullptr) write_meta(g_identity, m, *field);
    }
    H5Gclose(g_identity);

    H5Fclose(file);
    return true;
}

#endif  // BUILD_PHOTON_HDF

}  // namespace io
}  // namespace tttrlib
