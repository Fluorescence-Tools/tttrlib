// SPDX-License-Identifier: BSD-3-Clause
#include "io_hdf5.h"

#include <algorithm>
#include <cstring>
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

void write_dataset(hid_t loc, const char* name, hid_t file_type,
                   hid_t mem_type, hsize_t n, const void* data) {
    const hid_t space = H5Screate_simple(1, &n, nullptr);
    const hid_t ds = H5Dcreate2(loc, name, file_type, space,
                                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    if (n > 0) H5Dwrite(ds, mem_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
    H5Dclose(ds);
    H5Sclose(space);
}

void write_scalar(hid_t loc, const char* name, hid_t file_type,
                  hid_t mem_type, const void* data) {
    const hid_t space = H5Screate(H5S_SCALAR);
    const hid_t ds = H5Dcreate2(loc, name, file_type, space,
                                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(ds, mem_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, data);
    H5Dclose(ds);
    H5Sclose(space);
}

void write_scalar_int(hid_t loc, const char* name, int v) {
    write_scalar(loc, name, H5T_STD_I32LE, H5T_NATIVE_INT32, &v);
}

void write_string(hid_t loc, const char* name, const std::string& s) {
    const hid_t str_type = H5Tcopy(H5T_C_S1);
    H5Tset_size(str_type, std::max<std::size_t>(1, s.size()));
    H5Tset_strpad(str_type, H5T_STR_NULLPAD);
    const hid_t space = H5Screate(H5S_SCALAR);
    const hid_t ds = H5Dcreate2(loc, name, str_type, space,
                                H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    H5Dwrite(ds, str_type, H5S_ALL, H5S_ALL, H5P_DEFAULT, s.c_str());
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

    const hid_t g_photon_data = H5Gcreate2(file, "/photon_data",
                                           H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    const hsize_t n = static_cast<hsize_t>(n_events);
    write_dataset(g_photon_data, "timestamps", H5T_STD_U64LE, H5T_NATIVE_UINT64, n, macro_times);
    write_dataset(g_photon_data, "detectors", H5T_STD_I8LE, H5T_NATIVE_INT8, n, routing_channels);
    write_dataset(g_photon_data, "nanotimes", H5T_STD_U16LE, H5T_NATIVE_UINT16, n, micro_times);

    // Specs groups, so resolutions and the micro time channel count survive.
    const hid_t g_ts_specs = H5Gcreate2(g_photon_data, "timestamps_specs",
                                        H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    write_scalar(g_ts_specs, "timestamps_unit", H5T_IEEE_F64LE,
                 H5T_NATIVE_DOUBLE, &setup.timestamps_unit);
    H5Gclose(g_ts_specs);

    const hid_t g_nt_specs = H5Gcreate2(g_photon_data, "nanotimes_specs",
                                        H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    write_scalar(g_nt_specs, "tcspc_unit", H5T_IEEE_F64LE,
                 H5T_NATIVE_DOUBLE, &setup.tcspc_unit);
    write_scalar_int(g_nt_specs, "tcspc_num_bins", setup.tcspc_num_bins);
    H5Gclose(g_nt_specs);

    H5Gclose(g_photon_data);

    // ---- mandatory root fields ------------------------------------------
    write_string(file, "description", setup.description);
    double acquisition_duration = 0.0;
    if (n_events > 0) {
        acquisition_duration =
                static_cast<double>(macro_times[n_events - 1] - macro_times[0]) *
                setup.timestamps_unit;
    }
    write_scalar(file, "acquisition_duration", H5T_IEEE_F64LE,
                 H5T_NATIVE_DOUBLE, &acquisition_duration);

    // ---- /setup ----------------------------------------------------------
    const hid_t g_setup = H5Gcreate2(file, "/setup", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    write_scalar_int(g_setup, "num_pixels", setup.num_pixels);
    write_scalar_int(g_setup, "num_spots", setup.num_spots);
    write_scalar_int(g_setup, "num_spectral_ch", setup.num_spectral_ch);
    write_scalar_int(g_setup, "num_polarization_ch", setup.num_polarization_ch);
    write_scalar_int(g_setup, "num_split_ch", setup.num_split_ch);
    const int8_t modulated = setup.modulated_excitation ? 1 : 0;
    const int8_t lifetime = setup.lifetime ? 1 : 0;
    const int8_t alternated = setup.excitation_alternated ? 1 : 0;
    write_scalar(g_setup, "modulated_excitation", H5T_STD_I8LE, H5T_NATIVE_INT8, &modulated);
    write_scalar(g_setup, "lifetime", H5T_STD_I8LE, H5T_NATIVE_INT8, &lifetime);
    write_dataset(g_setup, "excitation_alternated", H5T_STD_I8LE, H5T_NATIVE_INT8, 1, &alternated);
    H5Gclose(g_setup);

    // ---- /identity -------------------------------------------------------
    const hid_t g_identity = H5Gcreate2(file, "/identity", H5P_DEFAULT, H5P_DEFAULT, H5P_DEFAULT);
    write_string(g_identity, "format_name", format_name);
    write_string(g_identity, "format_version", format_version);
    write_string(g_identity, "format_url", format_url);
    write_string(g_identity, "software", setup.software);
    write_string(g_identity, "software_version", setup.software_version);
    const std::time_t now = std::time(nullptr);
    char time_buffer[32];
    std::strftime(time_buffer, sizeof(time_buffer), "%Y-%m-%d %H:%M:%S", std::localtime(&now));
    write_string(g_identity, "creation_time", time_buffer);
    H5Gclose(g_identity);

    H5Fclose(file);
    return true;
}

#endif  // BUILD_PHOTON_HDF

}  // namespace io
}  // namespace tttrlib
