// SPDX-License-Identifier: BSD-3-Clause
#include "TTTR.h"
#include "TTTRRange.h"
#include "TTTRHeader.h"
#include "TTTRTags.h"
#include "io_sm.h"
#include "io_cz.h"
#include "io_bh.h"
#include "io_pq.h"
#include "FileCheck.h"
#include "Verbose.h"
#include "io_hdf5.h"

#include <nlohmann/json.hpp>

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif



TTTRHeader::TTTRHeader() :
        json_data_(new nlohmann::json()),
        header_end(0)
{
    json_data() = nlohmann::json::object();
    json_data()["tags"] = nlohmann::json::array();
    json_data()[TTTRContainerType] = 0;
    json_data()[TTTRRecordType] = -1;
if (is_verbose()) {
    std::clog << "-- TTTRHeader::TTTRHeader" << std::endl;
}
}

TTTRHeader::TTTRHeader(const TTTRHeader &p2) :
        json_data_(new nlohmann::json())
{
if (is_verbose()) {
    std::clog << "-- TTTRHeader::TTTRHeader - Copy constructor" << std::endl;
}
    json_data() = p2.json_data();
    header_end = p2.header_end;
}

TTTRHeader::TTTRHeader(
        std::FILE *fpin,
        int tttr_container_type,
        bool close_file,
        std::uint64_t base
        ) : TTTRHeader(tttr_container_type)
{
    // open_file() returns nullptr for a path that is not there, having already
    // said so on stderr, and TTTRHeader(filename, type) hands that straight to
    // this constructor. Every reader below seeks and reads without checking, so
    // going on is a segmentation fault rather than an error. Keep the default
    // header for the container instead -- the same answer container_records()
    // gives for the same input.
    if (fpin == nullptr) return;

    // An embedded container starts at `base`, so the readers are positioned
    // there and told not to rewind. They report header_end with ftell, which is
    // absolute, so everything downstream needs no further adjustment.
    const bool rewind = base == 0;
    if (!rewind) std::fseek(fpin, static_cast<long>(base), SEEK_SET);
if (is_verbose()) {
    std::clog << "-- TTTRHeader::TTTRHeader - Opening file" << std::endl;
    std::clog << "reading header" << std::endl;
}
    int tttr_record_type;
    if(tttr_container_type == PQ_PTU_CONTAINER){
        header_end = read_ptu_header(fpin, tttr_record_type, json_data(), rewind);
        int RecordType = get_tag(json_data(), "TTResultFormat_TTTRRecType")["value"];
        switch (RecordType)
        {
            case rtPicoHarpT2:
                tttr_record_type = PQ_RECORD_TYPE_PHT2;
                break;
            case rtPicoHarpT3:
                tttr_record_type = PQ_RECORD_TYPE_PHT3;
                break;
            case rtHydraHarpT2:
                tttr_record_type = PQ_RECORD_TYPE_HHT2v1;
                break;
            case rtHydraHarpT3:
                tttr_record_type = PQ_RECORD_TYPE_HHT3v1;
                break;
            case rtHydraHarp2T2:
            case rtTimeHarp260NT2:
            case rtTimeHarp260PT2:
                tttr_record_type = PQ_RECORD_TYPE_HHT2v2;
                break;
            case rtMultiHarpT2:
                tttr_record_type = PQ_RECORD_TYPE_GENERIC_T2;
                break;
            case rtHydraHarp2T3:
            case rtTimeHarp260NT3:
            case rtTimeHarp260PT3:
                tttr_record_type = PQ_RECORD_TYPE_HHT3v2;
                break;
            case rtMultiHarpT3:
                tttr_record_type = PQ_RECORD_TYPE_GENERIC_T3;
                break;
            default:
                tttr_record_type = PQ_RECORD_TYPE_HHT3v2;
        }
    } else if(tttr_container_type == CZ_CONFOCOR3_CONTAINER) {
        header_end = read_cz_confocor3_header(fpin, json_data(), rewind);
        tttr_record_type = get_tag(json_data(), TTTRRecordType)["value"];
    } else if(tttr_container_type == SM_CONTAINER){
        header_end = read_sm_header(fpin, json_data());
        tttr_record_type = get_tag(json_data(), TTTRRecordType)["value"];
    }
    else if(tttr_container_type == PQ_HT3_CONTAINER){
        header_end = read_ht3_header(fpin, json_data(), rewind);
        tttr_record_type = get_tag(json_data(), TTTRRecordType)["value"];
    } else if(tttr_container_type == BH_SPC600_256_CONTAINER){
        header_end = 0;
        add_tag(json_data(), TTTRTagGlobRes, 1.0, tyFloat8);
        add_tag(json_data(), TTTRNMicroTimes, 256, tyInt8);
        add_tag(json_data(), TTTRTagBits, 32, tyInt8);
        tttr_record_type = BH_RECORD_TYPE_SPC600_256;
    } else if(tttr_container_type == BH_SPC600_4096_CONTAINER){
        header_end = 0;
        add_tag(json_data(), TTTRTagGlobRes, 1.0, tyFloat8);
        add_tag(json_data(), TTTRNMicroTimes, 4096, tyInt8);
        add_tag(json_data(), TTTRTagBits, 48, tyInt8);
        tttr_record_type = BH_RECORD_TYPE_SPC600_4096;
    } else if(tttr_container_type == BH_SPC130_CONTAINER){
        header_end = read_bh132_header(fpin, json_data(), rewind);
        tttr_record_type = BH_RECORD_TYPE_SPC130;
    } else if(tttr_container_type == BH_SPCQC_CONTAINER){
        header_end = read_bh_spcqc_header(fpin, json_data(), rewind);
        // QC-x04 and QC-x06 differ in the channel width; the header picks one
        tttr_record_type = get_tag(json_data(), TTTRRecordType)["value"];
    } else{
        header_end = 0;
        add_tag(json_data(), TTTRTagBits, 32, tyInt8);
        tttr_record_type = BH_RECORD_TYPE_SPC130;
    }
    set_tttr_record_type(tttr_record_type);
    if(close_file) fclose(fpin);
if (is_verbose()) {
    std::clog << "End of header: " << header_end << std::endl;
    std::clog << json_data() << std::endl;
}
}


TTTRHeader::TTTRHeader(
        std::string fn,
        int tttr_container_type
) : TTTRHeader(open_file(fn, "r"), tttr_container_type, true) {

}


TTTRHeader::TTTRHeader(int tttr_container_type) : TTTRHeader(){
    set_tttr_container_type(tttr_container_type);
};


TTTRHeader& TTTRHeader::operator=(const TTTRHeader &p2){
    if (this != &p2) {
        json_data() = p2.json_data();
        header_end = p2.header_end;
    }
    return *this;
}


// Defined here, not in the class body: `json_data_` points at an incomplete
// type in TTTRHeader.h, so unique_ptr's deleter cannot be instantiated there.
TTTRHeader::~TTTRHeader() = default;


nlohmann::json& TTTRHeader::json_data(){
    return *json_data_;
}

const nlohmann::json& TTTRHeader::json_data() const {
    return *json_data_;
}


// ---------------------------------------------------------------------------
// Accessors that used to be inline in TTTRHeader.h. They live here so the
// header needs only <nlohmann/json_fwd.hpp>; none of them is on a per-photon
// path -- they are read once per file.
// ---------------------------------------------------------------------------

int TTTRHeader::get_tttr_record_type(){
    return (int) json_data()[TTTRRecordType];
}

void TTTRHeader::set_tttr_record_type(int v){
    json_data()[TTTRRecordType] = v;
}

int TTTRHeader::get_tttr_container_type(){
    return (int) json_data()[TTTRContainerType];
}

void TTTRHeader::set_tttr_container_type(int v){
    json_data()[TTTRContainerType] = v;
}

size_t TTTRHeader::get_bytes_per_record(){
    return (size_t) get_tag(json_data(), TTTRTagBits)["value"] / 8;
}

size_t TTTRHeader::size(){
    return json_data()["tags"].size();
}

nlohmann::json& TTTRHeader::operator[](std::size_t idx){
    return json_data()["tags"][idx];
}

const nlohmann::json& TTTRHeader::operator[](std::size_t idx) const {
    return json_data()["tags"][idx];
}

unsigned int TTTRHeader::get_number_of_micro_time_channels(){
    int v = get_tag(json_data(), TTTRNMicroTimes)["value"];
    if(v < 0){
        return 0;
    } else{
        return v;
    }
}

double TTTRHeader::get_micro_time_resolution(){
    return get_tag(json_data(), TTTRTagRes)["value"];
}

void TTTRHeader::set_micro_time_resolution(double resolution){
    TTTRHeader::add_tag(json_data(), TTTRTagRes, resolution, tyFloat8, -1);
}

void TTTRHeader::set_macro_time_resolution(double resolution){
    TTTRHeader::add_tag(json_data(), TTTRTagGlobRes, resolution, tyFloat8, -1);
}

void TTTRHeader::set_number_of_micro_time_channels(int n_channels){
    TTTRHeader::add_tag(json_data(), TTTRNMicroTimes, n_channels, tyInt8, -1);
}

void TTTRHeader::set_float_tag(const std::string& name, double value){
    TTTRHeader::add_tag(json_data(), name, value, tyFloat8, -1);
}

void TTTRHeader::set_int_tag(const std::string& name, int value){
    TTTRHeader::add_tag(json_data(), name, value, tyInt8, -1);
}

void TTTRHeader::set_blob_tag(const std::string& name, const std::vector<int32_t>& value){
    TTTRHeader::add_tag(json_data(), name, value, tyBinaryBlob, -1);
}

void TTTRHeader::set_string_tag(const std::string& name, const std::string& value){
    std::string copy = value;
    TTTRHeader::add_tag(json_data(), name, const_cast<char*>(copy.c_str()), tyAnsiString, -1);
}

int TTTRHeader::get_pixel_duration(){
    double pixel_duration_d = 0.0;
    auto tpp = TTTRHeader::get_tag(json_data(), "ImgHdr_TimePerPixel");
    if (!tpp.is_null() && tpp.contains("value") && !tpp["value"].is_null())
        pixel_duration_d = tpp["value"].get<double>();
    else
        pixel_duration_d = TTTRHeader::get_tag(
                json_data(), "$TimePerPixel")["value"];
    double global_res = TTTRHeader::get_tag(
            json_data(), "MeasDesc_GlobalResolution")["value"];
    // Round to nearest integer duration in macro clock units and cast explicitly to int
    int pixel_duration = static_cast<int>(std::llround(pixel_duration_d / global_res));
    return pixel_duration;
}

int TTTRHeader::get_line_duration(){
    double pixel_duration_d = 0.0;
    auto tpp = TTTRHeader::get_tag(json_data(), "ImgHdr_TimePerPixel");
    if (!tpp.is_null() && tpp.contains("value") && !tpp["value"].is_null())
        pixel_duration_d = tpp["value"].get<double>();
    else
        pixel_duration_d = TTTRHeader::get_tag(
                json_data(), "$TimePerPixel")["value"];
    double global_res_d = TTTRHeader::get_tag(
            json_data(), "MeasDesc_GlobalResolution")["value"];
    double n_pixel = TTTRHeader::get_tag(json_data(), "ImgHdr_PixX")["value"];
    int line_duration = static_cast<int>(std::ceil((pixel_duration_d * n_pixel) / global_res_d));
    return line_duration;
}

void TTTRHeader::set_json(std::string json_string){
    json_data() = nlohmann::json::parse(json_string);
}








// Minimal, dependency-free base64 codec used to carry the raw (largely binary)
// BH .set file through text-only header tags such as a PTU ANSI-string tag.



















/*!
 * \brief Read the Photon-HDF5 metadata groups into tags.
 *
 * The HDF5 half lives in io_hdf5 and comes back as a flat list of values, so
 * nothing here knows what a HighFive::Group is. That is the point: this header
 * had to forward-declare one for a single private method, which put an HDF5
 * toolchain on the include path of every consumer of the photon-stream data
 * model.
 *
 * Metadata is reported rather than mapped -- Photon-HDF5 does not fix the names
 * or types under /setup and /identity -- so whatever the file carries is kept as
 * a "<group>.<name>" tag, and the four values tttrlib does understand are
 * additionally promoted to their canonical tag names.
 */
int TTTRHeader::read_photon_hdf5_setup(const char *fn) {
    if (!tttrlib::io::photon_hdf5_available()) return -1;

    const auto values = tttrlib::io::read_photon_hdf5_metadata(std::string(fn ? fn : ""));
    json_data()["MeasDesc_ContainerType"] = PHOTON_HDF_CONTAINER;

    for (const auto& v : values) {
        // Root-level fields (description, acquisition_duration) have no group,
        // and a leading dot is not a name.
        const std::string name = v.group.empty() ? v.name : v.group + "." + v.name;
        switch (v.kind) {
            case tttrlib::io::Hdf5Value::Kind::Int:
                add_tag(json_data(), name, (int) v.i, tyInt8, v.index);
                break;
            case tttrlib::io::Hdf5Value::Kind::Float:
                add_tag(json_data(), name, v.d, tyFloat8, v.index);
                break;
            case tttrlib::io::Hdf5Value::Kind::String: {
                // add_tag takes std::any and does any_cast<char*>, so a
                // `const char*` is a different type and throws bad_any_cast --
                // which the TTTR constructor's catch(...) then reports as
                // "container type not supported". A mutable buffer it is.
                std::vector<char> buf(v.s.begin(), v.s.end());
                buf.push_back('\0');
                add_tag(json_data(), name, buf.data(), tyAnsiString, v.index);
                break;
            }
        }
    }

    // Promote the values that mean something to the rest of tttrlib. Guarded
    // individually: a file may carry one specs group and not the other.
    //
    // The explicit index 0 matters. find_tag matches the stored idx exactly and
    // defaults to -1, so the default finds nothing for a scalar stored at 0 --
    // unlike get_tag, whose -1 does match. Leaving it out silently skipped the
    // promotion and every Photon-HDF5 file read back with a macro time
    // resolution of -1.
    if (find_tag(json_data(), "timestamps_specs.timestamps_unit", 0) >= 0) {
        double v = get_tag(json_data(), "timestamps_specs.timestamps_unit")["value"];
        add_tag(json_data(), TTTRTagGlobRes, v, tyFloat8);
    }
    if (find_tag(json_data(), "nanotimes_specs.tcspc_num_bins", 0) >= 0) {
        int v = get_tag(json_data(), "nanotimes_specs.tcspc_num_bins")["value"];
        add_tag(json_data(), TTTRNMicroTimes, v, tyInt8);
    }
    if (find_tag(json_data(), "nanotimes_specs.tcspc_unit", 0) >= 0) {
        double v = get_tag(json_data(), "nanotimes_specs.tcspc_unit")["value"];
        add_tag(json_data(), TTTRTagRes, v, tyFloat8);
    }
    return 0;
}



void TTTRHeader::ensure_minimal_tags(
        TTTRHeader* header, int container_type, size_t n_records){
    nlohmann::json &json = header->json_data();

    // Macro time resolution (seconds). Several writers (SPC-132, HT3, SM, CZ)
    // read this directly; a missing value makes them emit a garbage clock, so
    // always guarantee a positive value.
    if(find_tag(json, TTTRTagGlobRes) < 0){
        double v = header->get_macro_time_resolution();
        if(!(v > 0.0)) v = 1.0;
        add_tag(json, TTTRTagGlobRes, v, tyFloat8);
    }

    // Micro time (Dtime) resolution (seconds).
    if(find_tag(json, TTTRTagRes) < 0){
        double v = header->get_micro_time_resolution();
        if(!(v > 0.0)) v = 1.0;
        add_tag(json, TTTRTagRes, v, tyFloat8);
    }

    // Number of micro time channels.
    if(find_tag(json, TTTRNMicroTimes) < 0){
        int n = (int) header->get_number_of_micro_time_channels();
        if(n <= 0) n = 1;
        add_tag(json, TTTRNMicroTimes, n, tyInt8);
    }

    // PTU carries the record encoding and count explicitly; without these a
    // conforming PTU reader cannot parse the record stream.
    if(container_type == PQ_PTU_CONTAINER){
        if(find_tag(json, TTTRTagBits) < 0)
            add_tag(json, TTTRTagBits, 32, tyInt8);
        if(find_tag(json, TTTRTagNumRecords) < 0)
            add_tag(json, TTTRTagNumRecords, (int) n_records, tyInt8);
    }
}



























double TTTRHeader::get_macro_time_resolution(){
    double res;
    auto tag = get_tag(json_data(), TTTRTagGlobRes);
    if(tag["name"] == "NONE"){
        res = 1. / (double) get_tag(json_data(), TTTRSyncRate)["value"];
    } else{
        res = tag["value"];
    }
    return res;
}


std::string TTTRHeader::get_json(std::string tag_name, int idx, int indent){
    std::string s;
    if(tag_name.empty()){
        s = json_data().dump(indent);
    } else{
        int tag_idx = find_tag(json_data(), tag_name, idx);
        if(tag_idx >= 0){
            s = json_data()["tags"][tag_idx].dump(indent);
        } else {
            s = "{}";
        }
    }
    return s;
}


// ---------------------------------------------------------------------------
// Tag access.
//
// The implementations moved to the io layer (io/TTTRTags.h): they manipulate a
// nlohmann::json document and touch no TTTRHeader state at all, while every
// vendor header reader needs them. Leaving them here would have forced a format
// module to depend on core, and core to depend on the format modules -- a link
// cycle CMake refuses between shared libraries.
//
// These remain the public static API they have always been.
// ---------------------------------------------------------------------------

void TTTRHeader::add_tag(nlohmann::json &json_data, const std::string &name,
                         std::any value, unsigned int type, int idx) {
    tttrlib::io::add_tag(json_data, name, std::move(value), type, idx);
}

nlohmann::json TTTRHeader::get_tag(const nlohmann::json &json_data,
                                   const std::string &name, int idx) {
    return tttrlib::io::get_tag(json_data, name, idx);
}

int TTTRHeader::find_tag(nlohmann::json &json_data, const std::string &name, int idx) {
    return tttrlib::io::find_tag(json_data, name, idx);
}

// --- vendor header readers/writers, now owned by their io_* modules ---------
// Declarations stay on TTTRHeader: they are public static API that the Java and
// Python bindings both expose. Only the implementations moved.

size_t TTTRHeader::read_sm_header(FILE* file, nlohmann::json &j) {
    return tttrlib::io::read_sm_header(file, j);
}

void TTTRHeader::write_sm_header(std::string fn, TTTRHeader* header, std::string modes) {
    tttrlib::io::write_sm_header(std::move(fn), header->json_data(), std::move(modes));
}

size_t TTTRHeader::read_cz_confocor3_header(std::FILE *fpin, nlohmann::json &data, bool rewind) {
    return tttrlib::io::read_cz_confocor3_header(fpin, data, rewind);
}

void TTTRHeader::write_cz_confocor3_header(std::string fn, TTTRHeader* header, std::string modes) {
    tttrlib::io::write_cz_confocor3_header(std::move(fn), header->json_data(), std::move(modes));
}

size_t TTTRHeader::read_bh132_header(std::FILE *fpin, nlohmann::json &data, bool rewind) {
    return tttrlib::io::read_bh132_header(fpin, data, rewind);
}

size_t TTTRHeader::read_bh_spcqc_header(std::FILE *fpin, nlohmann::json &data, bool rewind) {
    return tttrlib::io::read_bh_spcqc_header(fpin, data, rewind);
}

bool TTTRHeader::read_bh_set_file(const std::string& filename) {
    return tttrlib::io::read_bh_set_file(filename, json_data());
}

bool TTTRHeader::parse_bh_set(const std::string& content) {
    return tttrlib::io::parse_bh_set(content, json_data());
}

bool TTTRHeader::write_bh_set_file(const std::string& filename, TTTRHeader* header) {
    return tttrlib::io::write_bh_set_file(filename, header->json_data());
}

void TTTRHeader::write_spc132_header(std::string fn, TTTRHeader* header, std::string modes) {
    tttrlib::io::write_spc132_header(std::move(fn), header->json_data(), std::move(modes));
}

void TTTRHeader::write_spcqc_header(std::string fn, TTTRHeader* header, std::string modes) {
    tttrlib::io::write_spcqc_header(std::move(fn), header->json_data(), std::move(modes));
}

size_t TTTRHeader::read_ptu_header(std::FILE *fpin, int &tttr_record_type,
                                   nlohmann::json &data, bool rewind) {
    return tttrlib::io::read_ptu_header(fpin, tttr_record_type, data, rewind);
}

size_t TTTRHeader::read_ht3_header(std::FILE *fpin, nlohmann::json &data, bool rewind) {
    return tttrlib::io::read_ht3_header(fpin, data, rewind);
}

void TTTRHeader::write_ptu_header(std::string fn, TTTRHeader* header, std::string modes) {
    tttrlib::io::write_ptu_header(std::move(fn), header->json_data(), std::move(modes));
}

void TTTRHeader::write_ht3_header(std::string fn, TTTRHeader* header, std::string modes) {
    tttrlib::io::write_ht3_header(std::move(fn), header->json_data(), std::move(modes));
}
