// SPDX-License-Identifier: BSD-3-Clause
#include "TTTR.h"

#include <nlohmann/json.hpp>

#include "TTTRHeader.h"
#include "TTTRHeaderTypes.h"
#include "TTTRFormat.h"
#include "PluginHost.h"
#include "io_be.h"
#include "io_fl.h"
#include "io_hdf5.h"
#include "TTTRMask.h"
#include "FileCheck.h"
#include "PhotonscoreD7.h"
#include "Verbose.h"

#include <cstdlib>
#include <cstring>
#include <ctime>
#include <algorithm>
#include <array>

// Static member definition outside the class
tttrlib::bimap<std::string, int> TTTR::initialize_container_names() {
    tttrlib::bimap<std::string, int> m;
    for (const auto& f : tttrlib::IORegistry::formats()) {
        m.insert({f.name, f.container_type});
    }
    return m;
}

/*!
 * \brief The name-to-id map, rebuilt whenever the format table changes.
 *
 * It used to be built once, on first use, which was correct while the set of
 * formats was fixed at compile time. A plugin adds a row at run time, and the
 * first thing that touches this map decides forever whether that row is
 * visible -- so a plugin format could be registered, listed in the registry,
 * and still not nameable in `TTTR(filename, "MYLAB")`. The generation counter
 * costs one comparison per lookup and removes the ordering dependency.
 *
 * Loading is triggered here, too, rather than at import: this is the narrowest
 * point every path that could need a plugin passes through.
 */
tttrlib::bimap<std::string, int>& TTTR::container_names() {
    static tttrlib::bimap<std::string, int> names;
    static unsigned long built_at = static_cast<unsigned long>(-1);
    tttrlib::PluginHost::ensure_loaded();
    const unsigned long now = tttrlib::IORegistry::generation();
    if (built_at != now) {
        // Refilled in place rather than replaced: bimap's view proxies hold a
        // reference to their owner, so it is not assignable, and a reference
        // already handed out has to keep pointing at the live map anyway.
        names.clear();
        for (const auto& f : tttrlib::IORegistry::formats()) {
            names.insert({f.name, f.container_type});
        }
        built_at = now;
    }
    return names;
}
bool TTTR::auto_compress_on_read = []() {
    bool enabled = tttrlib::env::init_auto_compress_on_read();
    if (!enabled && is_verbose()) {
        std::clog << "TTTR: auto-compress on read disabled via TTTR_COMPRESS_ON_READ" << std::endl;
    }
    return enabled;
}();


TTTR::TTTR() :
        // private
        filename(),
        header(nullptr),
        tttr_container_type(-1),
        tttr_container_type_str(),
        tttr_record_type(-1),
        overflow_counter(0),
        fp_records_begin(0),
        TTTRRecord(0),
        macro_times(nullptr),
        macro_times_compressed(nullptr),
        macro_time_keyframes(nullptr),
        n_keyframes(0),
        keyframe_interval(1000000),
        macro_time_compression_enabled(false),
        mt_linearizer(new MicrotimeLinearization()),
        micro_times(nullptr),
        routing_channels(nullptr),
        event_types(nullptr),
        // protected
        n_records_in_file(0),
        n_records_read(0),
        n_valid_events(0){
    header = new TTTRHeader(tttr_container_type);
    allocate_memory_for_records(0);
}

TTTR::TTTR(unsigned long long *macro_times, int n_macrotimes,
           unsigned short *micro_times, int n_microtimes,
           signed char *routing_channels, int n_routing_channels,
           signed char *event_types, int n_event_types,
           bool find_used_channels
): TTTR() {
if (is_verbose()) {
    std::clog << "INITIALIZING FROM VECTORS" << std::endl;
}
    this->filename = "NA";
    size_t n_elements;
    if (!(n_macrotimes == n_microtimes &&
          n_macrotimes == n_routing_channels &&
          n_macrotimes == n_event_types)
    ) {
        // the selection does not match the dimension of the parent
        n_elements = std::min(n_macrotimes, std::min(
                n_microtimes, std::min(
                        n_routing_channels, n_event_types
                )));
        std::clog << "WARNING: The input vectors differ in size. Using " << std::endl;
    } else{
        n_elements = n_macrotimes;
    }
    allocate_memory_for_records(n_elements);
    n_valid_events = n_elements;
    for(size_t i=0; i<n_elements; i++){
        set_macro_time_at(i, macro_times[i]);
        this->micro_times[i] = micro_times[i];
        this->event_types[i] = event_types[i];
        this->routing_channels[i] = routing_channels[i];
    }
    if(find_used_channels) find_used_routing_channels();
}

TTTR::TTTR(
        const TTTR &parent,
        int *selection,
        int n_selection,
        bool find_used_channels) :  TTTR()
        {
if (is_verbose()) {
    std::clog << "INITIALIZING FROM SELECTION" << std::endl;
}
    copy_from(parent, false);
    n_valid_events = (size_t) n_selection;
    if ((size_t) n_selection > parent.n_valid_events) {
        std::clog << "WARNING: The dimension of the selection exceeds the parents dimension." << std::endl;
    }
    
    // Check if selection is sequential (compression-compatible)
    // Compression requires monotonically increasing macro times
    bool is_sequential = true;
    std::vector<int> resolved_selection(n_selection);
    for(size_t i = 0; i < n_selection; i++){
        int sel = selection[i];
        sel = (sel < 0) ? static_cast<int>(parent.n_valid_events) + sel : sel;
        resolved_selection[i] = sel;
        if(i > 0 && sel < resolved_selection[i-1]){
            is_sequential = false;
        }
    }
    
    // Disable compression for this instance if selection is not sequential
    // Must be done BEFORE allocate_memory_for_records to prevent compressed allocation
    if(!is_sequential){
        macro_time_compression_enabled = false;
    } else {
        // Keep parent's compression state for sequential selections
        macro_time_compression_enabled = parent.macro_time_compression_enabled;
    }
    
    allocate_memory_for_records(n_selection);
    
    for(size_t sel_i = 0; sel_i < n_selection; sel_i++){
        int sel = resolved_selection[sel_i];
        set_macro_time_at(sel_i, parent.get_macro_time_at(sel));
        micro_times[sel_i] = parent.micro_times[sel];
        event_types[sel_i] = parent.event_types[sel];
        routing_channels[sel_i] = parent.routing_channels[sel];
    }
    
    if(find_used_channels) find_used_routing_channels();
}

void TTTR::copy_from(const TTTR &p2, bool include_big_data) {
    filename = p2.filename;
    header = new TTTRHeader(*p2.header);
    tttr_container_type = p2.tttr_container_type;
    tttr_container_type_str = p2.tttr_container_type_str;
    tttr_container_parameters = p2.tttr_container_parameters;
    fp_records_begin = p2.fp_records_begin;

    used_routing_channels = p2.used_routing_channels;
    n_records_in_file = p2.n_records_in_file;
    n_records_read = p2.n_records_read;
    n_valid_events = p2.n_valid_events;
    fp_records_begin = p2.fp_records_begin;
    
    // Copy new member variables for LUTs and shifts
    if (mt_linearizer != nullptr) {
        delete mt_linearizer;
    }
    if (p2.mt_linearizer != nullptr) {
        mt_linearizer = new MicrotimeLinearization(*p2.mt_linearizer);
    } else {
        mt_linearizer = new MicrotimeLinearization();
    }
    // Copy compression-related fields
    keyframe_interval = p2.keyframe_interval;
    macro_time_compression_enabled = p2.macro_time_compression_enabled;
    n_keyframes = p2.n_keyframes;
    
    if (include_big_data){
        allocate_memory_for_records(p2.n_valid_events);
        for (size_t i = 0; i < p2.n_valid_events; i++) {
            set_macro_time_at(i, p2.get_macro_time_at(i));
            micro_times[i] = p2.micro_times[i];
            routing_channels[i] = p2.routing_channels[i];
            event_types[i] = p2.event_types[i];
        }
    }
}

TTTR::TTTR(const TTTR &p2){
    copy_from(p2, true);
}

TTTR::TTTR(const char *filename, int container_type, bool read_input) : TTTR(){
    if(container_type >= 0){
        tttr_container_type_str = container_names().right.at(container_type);
        tttr_container_type = container_type;
        this->filename.assign(filename);
        if(read_input){
            if(read_file())
                find_used_routing_channels();
        }
    } else{
        std::cerr << "File " << filename << " not supported." << std::endl;
    }
}

TTTR::TTTR(const char *filename, int container_type, 
           const std::map<int, std::vector<float>>& channel_luts,
           const std::map<signed char, int>& channel_shifts,
           bool read_input) : TTTR(){
    if(container_type >= 0){
        tttr_container_type_str = container_names().right.at(container_type);
        tttr_container_type = container_type;
        this->filename.assign(filename);
        // Configure LUTs and shifts directly in MicrotimeLinearization
        if (!channel_luts.empty() || !channel_shifts.empty()) {
            apply_channel_luts(channel_luts, channel_shifts);
        }
        if(read_input){
            if(read_file()){
                find_used_routing_channels();
                // Apply LUTs and shifts
                apply_luts_and_shifts(-1, true);
            }
        }
    } else{
        std::cerr << "File " << filename << " not supported." << std::endl;
    }
}

TTTR::TTTR(const char *fn, const char *container_type, bool read_input) : TTTR() {
    try {
        std::string container_type_str_lower(container_type);
        std::transform(container_type_str_lower.begin(), container_type_str_lower.end(),
                       container_type_str_lower.begin(), ::tolower);

        if (container_type_str_lower == "auto") {
            tttr_container_type = inferTTTRFileType(fn);
            tttr_container_type_str = container_names().right.at(tttr_container_type);
        } else {
            tttr_container_type_str.assign(container_type);
            tttr_container_type = container_names().left.at(std::string(container_type));
        }

        filename.assign(fn);
        if (read_input && read_file())
            find_used_routing_channels();
    }
    catch (...) {
        std::cerr << "TTTR::TTTR(const char *fn, const char *container_type, bool read_input): "
                  << "Container type " << container_type << " not supported." << std::endl;
    }
}

TTTR::TTTR(const char *fn, const char *container_type) : TTTR() {
    try {
        std::string container_type_str_lower(container_type);
        std::transform(container_type_str_lower.begin(), container_type_str_lower.end(),
                       container_type_str_lower.begin(), ::tolower);

        if (container_type_str_lower == "auto") {
            tttr_container_type = inferTTTRFileType(fn);
            tttr_container_type_str = container_names().right.at(tttr_container_type);
        } else {
            tttr_container_type_str.assign(container_type);
            tttr_container_type = container_names().left.at(std::string(container_type));
        }

        filename.assign(fn);
        if (read_file())
            find_used_routing_channels();
    }
    catch (...) {
        std::cerr << "TTTR::TTTR(const char *fn, const char *container_type): "
                  << "Container type " << container_type << " not supported." << std::endl;
    }
}

TTTR::TTTR(const char *fn, const char *container_type,
           const std::string& parameters, bool read_input) : TTTR() {
    tttr_container_parameters = parameters;
    try {
        std::string lowered(container_type);
        std::transform(lowered.begin(), lowered.end(), lowered.begin(), ::tolower);
        if (lowered == "auto") {
            tttr_container_type = inferTTTRFileType(fn);
            tttr_container_type_str = container_names().right.at(tttr_container_type);
        } else {
            tttr_container_type_str.assign(container_type);
            tttr_container_type = container_names().left.at(std::string(container_type));
        }
        filename.assign(fn);
        if (read_input && read_file())
            find_used_routing_channels();
    }
    catch (...) {
        std::cerr << "TTTR::TTTR(fn, container_type, parameters, read_input): "
                  << "Container type " << container_type << " not supported." << std::endl;
    }
}

TTTR::TTTR(const char *fn, int container_type,
           const std::string& parameters, bool read_input) : TTTR() {
    tttr_container_parameters = parameters;
    if (container_type >= 0) {
        tttr_container_type_str = container_names().right.at(container_type);
        tttr_container_type = container_type;
        this->filename.assign(fn);
        if (read_input && read_file())
            find_used_routing_channels();
    } else {
        std::cerr << "File " << fn << " not supported." << std::endl;
    }
}

TTTR::TTTR(const char* filename) : TTTR(filename, inferTTTRFileType(filename), true) {}


void TTTR::shift_macro_time(int shift) {
    for(size_t i=0; i<n_valid_events; i++){
        set_macro_time_at(i, get_macro_time_at(i) + shift);
    }
}

void TTTR::find_used_routing_channels() {
    used_routing_channels.clear();
    // A routing channel is one byte, so a fixed lookup table avoids scanning
    // the growing result vector for every event.  Keep the first-seen order
    // for API compatibility.
    std::array<bool, 256> seen{};
    for (size_t i = 0; i < n_valid_events; i++) {
        signed char channel = routing_channels[i];
        const auto key = static_cast<unsigned char>(channel);
        if (!seen[key]) {
            seen[key] = true;
            used_routing_channels.push_back(channel);
        }
    }
}

/*!
 * \brief Read a Photon-HDF5 file into the standard event arrays.
 *
 * The decode lives in io_hdf5; this turns its output into tttrlib's internal
 * representation. Photon-HDF5 stores decoded arrays rather than an instrument's
 * record encoding, so nothing here resembles a record parser -- the whole job is
 * copying, and honouring macro time compression while doing it.
 */
int TTTR::read_hdf_file(const char *fn) {
    if (!tttrlib::io::photon_hdf5_available()) {
        std::cerr << "Not built with Photon HDF interface." << std::endl;
        return 1;
    }
    header = new TTTRHeader();
    header->read_photon_hdf5_setup(fn);

    tttrlib::io::PhotonHdf5Photons p;
    try {
        p = tttrlib::io::read_photon_hdf5_photons(std::string(fn ? fn : ""));
    } catch (const std::exception &e) {
        std::cerr << "Error: " << e.what() << std::endl;
        return 1;
    }
    if (!p.has_timestamps) {
        std::cerr << "Warning: /photon_data/timestamps not found." << std::endl;
        return 1;
    }
    if (!p.has_detectors) {
        std::cerr << "Warning: /photon_data/detectors not found. "
                     "Filling routing_channels with zeros." << std::endl;
    }
    if (!p.has_nanotimes) {
        std::cerr << "Warning: /photon_data/nanotimes not found. "
                     "Filling micro_times with zeros." << std::endl;
    }

    const size_t n = p.size();
    n_valid_events = n;
    n_records_in_file = n;
    allocate_memory_for_records(n);
    for (size_t i = 0; i < n; ++i) {
        // set_macro_time_at rather than a memcpy: with macro time compression
        // enabled the array is not a flat list of ticks.
        set_macro_time_at(i, p.macro_times[i]);
        routing_channels[i] = p.routing_channels[i];
        micro_times[i] = p.micro_times[i];
    }
    return 0;
}

bool TTTR::write_hdf_file(std::string fn, TTTRHeader* header){
    if(header == nullptr) header = this->header;
    if (!tttrlib::io::photon_hdf5_available()) {
        std::cerr << "Not built with Photon HDF interface." << std::endl;
        return false;
    }

    tttrlib::io::PhotonHdf5Setup setup;
    setup.timestamps_unit = header->get_macro_time_resolution();
    setup.tcspc_unit = header->get_micro_time_resolution();
    setup.tcspc_num_bins = (int) header->get_number_of_micro_time_channels();

    // Values read from a source Photon-HDF5 file are preserved (the header
    // reader stores them as "setup.<name>" tags); single-spot defaults are used
    // otherwise, so a round trip does not quietly re-describe the instrument.
    auto setup_tag_int = [&header](const char* name, int d) -> int {
        std::string tag_name = std::string("setup.") + name;
        if (TTTRHeader::find_tag(header->json_data(), tag_name, 0) < 0) return d;
        auto v = TTTRHeader::get_tag(header->json_data(), tag_name, 0)["value"];
        if (v.is_boolean()) return (int) v.get<bool>();
        if (v.is_number()) return (int) v.get<double>();
        return d;
    };
    std::unordered_set<signed char> channels(
            routing_channels, routing_channels + n_valid_events);
    setup.num_pixels = setup_tag_int("num_pixels", std::max<int>(1, (int) channels.size()));
    setup.num_spots = setup_tag_int("num_spots", 1);
    setup.num_spectral_ch = setup_tag_int("num_spectral_ch", 1);
    setup.num_polarization_ch = setup_tag_int("num_polarization_ch", 1);
    setup.num_split_ch = setup_tag_int("num_split_ch", 1);
    setup.modulated_excitation = setup_tag_int("modulated_excitation", 0) != 0;
    setup.lifetime = setup_tag_int("lifetime", 1) != 0;
    setup.excitation_alternated = setup_tag_int("excitation_alternated", 0) != 0;

    // get_macro_time_at rather than the raw array, for the same reason the
    // reader uses set_macro_time_at.
    std::vector<uint64_t> timestamps(n_valid_events);
    for (size_t i = 0; i < n_valid_events; i++) timestamps[i] = get_macro_time_at(i);

    return tttrlib::io::write_photon_hdf5(
            fn, timestamps.data(), routing_channels, micro_times,
            n_valid_events, setup);
}


int TTTR::read_sm_file(const char *filename){
    // Function to read a 64-bit big-endian value

    // Open the file using Unicode-safe open_file
    FILE* fp = open_file(std::string(filename), "rb");
    if (!fp) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return 1;
    }

    // Decode header
    header = new TTTRHeader(fp, SM_CONTAINER);

    // Skip the header (165 bytes)
    size_t HEADER_SIZE = header->header_end;
    if (fseek(fp, static_cast<long>(HEADER_SIZE), SEEK_SET) != 0) {
        std::cerr << "Error seeking past the header." << std::endl;
        fclose(fp);
        return 1;
    }

    // Determine file size to calculate remaining data size
    // Use 64-bit file I/O to support files > 2GB on Windows
    fseek64(fp, 0, SEEK_END);
    int64_t fileSize = ftell64(fp);
    fseek64(fp, static_cast<int64_t>(HEADER_SIZE), SEEK_SET); // Return to start of data after header

    if (fileSize < 0 || static_cast<size_t>(fileSize) < HEADER_SIZE + 26) {
        std::cerr << "Error: File is too short to contain expected data and trailing bytes." << std::endl;
        fclose(fp);
        return 1;
    }

    size_t dataSize = fileSize - HEADER_SIZE - 26;

    // Allocate buffer to hold the data
    std::vector<uint8_t> buffer(dataSize);
    if (fread(buffer.data(), 1, dataSize, fp) != dataSize) {
        std::cerr << "Error reading data from file." << std::endl;
        fclose(fp);
        return 1;
    }

    fclose(fp);  // Close the file after reading

    // Define inline lambda functions for endian conversion
    auto readBigEndian64 = [](const uint8_t* data) -> uint64_t {
        return (static_cast<uint64_t>(data[0]) << 56) |
               (static_cast<uint64_t>(data[1]) << 48) |
               (static_cast<uint64_t>(data[2]) << 40) |
               (static_cast<uint64_t>(data[3]) << 32) |
               (static_cast<uint64_t>(data[4]) << 24) |
               (static_cast<uint64_t>(data[5]) << 16) |
               (static_cast<uint64_t>(data[6]) << 8)  |
               (static_cast<uint64_t>(data[7]));
    };

    // Function to read a 32-bit big-endian integer
    auto readBigEndian32 = [](const uint8_t* data) -> uint32_t {
        return (static_cast<uint32_t>(data[0]) << 24) |
               (static_cast<uint32_t>(data[1]) << 16) |
               (static_cast<uint32_t>(data[2]) << 8) |
               static_cast<uint32_t>(data[3]);
    };

    // Process the data in 12-byte records
    const size_t RECORD_SIZE = 12;
    if (buffer.size() % RECORD_SIZE != 0) {
        std::cerr << "Error: Data size is not a multiple of record size." << std::endl;
        return 1;
    }

    size_t numRecords = buffer.size() / RECORD_SIZE;
    n_valid_events = numRecords;
    n_records_in_file = n_valid_events;
    allocate_memory_for_records(numRecords);
    for (size_t i = 0; i < numRecords; ++i) {
        const uint8_t* record = buffer.data() + i * RECORD_SIZE;

        // Read and interpret the 64-bit PH time
        uint64_t macro_time = readBigEndian64(record);
        // Read and interpret the 16-bit detector

        uint32_t routing_channel = readBigEndian32(record + 8);

        macro_times[i] = macro_time;
        routing_channels[i] = routing_channel;
        micro_times[i] = 0;

    }

    return 0;

}

/*!
 * \brief Read a BrightEyes-TTM raw stream into the standard event arrays.
 *
 * The decode lives in io_be; this turns its output into tttrlib's
 * internal representation, so everything downstream -- selections, correlation,
 * CLSMImage -- works on a .ttr exactly as it would on a PTU.
 *
 * Two things are worth knowing about the result:
 *
 * - Macro times are in sample-clock ticks (240 MHz by default), not in the
 *   units of any other container. A .ttr carries no clock, so this is the only
 *   honest choice; the resolution tag records what was assumed.
 * - Micro times are raw TDC codes unless a calibration is supplied. The payload
 *   is a tapped-delay-line code whose bins are unequal, so scaling it by a
 *   single number would produce a plausible-looking wrong lifetime.
 */
int TTTR::read_ttr_file(const char *fn) {
    tttrlib::io::TtrParams params;   // instrument defaults; see io_be.h
    tttrlib::io::TtrData d;
    try {
        // Whatever the caller said the instrument is. Everything the format
        // cannot tell you about itself enters here and nowhere else.
        params = tttrlib::io::ttr_params_from_json(tttr_container_parameters);
        d = tttrlib::io::read_ttr(std::string(fn ? fn : ""), params);
    } catch (const std::exception &e) {
        std::cerr << "Error reading .ttr file: " << e.what() << std::endl;
        return 0;
    }

    header = new TTTRHeader(BE_TTR_CONTAINER);
    header->set_tttr_record_type(BE_RECORD_TYPE_TTR);
    // The sample clock is an assumption, so it is written down rather than left
    // implicit: a reader of the header can see what the macro times mean.
    header->set_macro_time_resolution(1.0 / (params.sysclk_MHz * 1e6));
    header->set_number_of_micro_time_channels(d.n_micro_time_channels);
    // Whether the micro times are times at all. A delay-line code is not
    // proportional to a duration, so a fit against uncalibrated codes is
    // meaningless -- and would look perfectly reasonable. Downstream can refuse
    // rather than guess, which it cannot do if the state is invisible.
    header->set_int_tag("BrightEyes_MicroTimeCalibrated", d.micro_times_calibrated ? 1 : 0);
    header->set_string_tag("BrightEyes_MicroTimeUnit",
                           d.micro_times_calibrated ? "picoseconds" : "tdc_code");
    if (d.micro_times_calibrated) {
        header->set_micro_time_resolution(d.micro_time_bin_ps * 1e-12);   // TTTRTagRes is seconds
    }

    const size_t n = d.event_types.size();
    allocate_memory_for_records(n);
    for (size_t i = 0; i < n; ++i) {
        set_macro_time_at(i, static_cast<unsigned long long>(d.macro_times[i]));
        micro_times[i] = d.micro_times[i];
        routing_channels[i] = d.routing_channels[i];
        event_types[i] = d.event_types[i];
    }
    n_records_read = n;
    n_valid_events = n;
    return 1;
}


/*!
 * \brief Read a FLIM LABS time-tagger file into the standard event arrays.
 *
 * The decode lives in io_fl; this turns its output into tttrlib's internal
 * representation and records, in the header, the two things about the result
 * that the file itself does not state:
 *
 * - what a macro time tick is. The file's times are floating-point
 *   nanoseconds, so a tick had to be chosen: the laser period for STT1, one
 *   picosecond for ITT1. See io_fl.h.
 * - which reading of the file's macro times turned out to be true, via
 *   FlimLabs_MacroTimeResidual_ns.
 *
 * Markers keep the format's own codes -- 70 'F', 76 'L', 80 'P' -- as their
 * routing channel, and are advertised through the same ImgHdr_* tags every
 * other imaging container uses, so CLSMImage configures itself without knowing
 * this format exists.
 */
/*!
 * \brief Read through a container a plugin contributed.
 *
 * The plugin decodes into buffers this side allocates and owns -- the one rule
 * that keeps a cross-runtime free mismatch structurally impossible on the path
 * that runs once per event. It fills a batch, says how many it wrote, and is
 * called again until it reports none.
 *
 * Everything a plugin can get wrong is contained here: a status becomes a
 * message attributed to the plugin, an over-long batch is clamped rather than
 * trusted, and an exception escaping the C boundary is caught rather than
 * unwinding through it.
 */
int TTTR::read_plugin_file(const char *fn, int container_type) {
    const tttrlib_container_v1* c = tttrlib::PluginHost::container_for(container_type);
    if (c == nullptr) {
        std::cerr << "ERROR: no plugin provides container " << container_type << std::endl;
        return 0;
    }

    const char* params = tttr_container_parameters.empty()
                             ? nullptr : tttr_container_parameters.c_str();
    void* handle = nullptr;
    auto fail = [&](const std::string& what) {
        std::cerr << "Error reading through plugin container '" << c->name << "': " << what;
        const std::string detail = tttrlib::PluginHost::last_error();
        if (!detail.empty()) std::cerr << " (" << detail << ")";
        std::cerr << std::endl;
        if (handle != nullptr && c->close != nullptr) c->close(c->ctx, handle);
        return 0;
    };

    try {
        if (c->open(c->ctx, fn ? fn : "", params, &handle) != TTTRLIB_OK) {
            return fail("open failed");
        }

        // Decoded into plain vectors first, and only then into the event store.
        //
        // Two reasons, both of which bite silently if ignored.
        // allocate_memory_for_records() builds a *fresh* store rather than
        // growing one, so calling it again between batches discards everything
        // already decoded. And macro times do not necessarily have a column at
        // all: with compression on -- the default -- they are deltas plus
        // keyframes, `macro_times` is null, and the only way in is
        // set_macro_time_at(). Staging is what lets the batch loop stay simple
        // and still be correct under both storage layouts.
        uint64_t hint = 0;
        if (c->event_count_hint != nullptr) {
            if (c->event_count_hint(c->ctx, handle, &hint) != TTTRLIB_OK) hint = 0;
        }
        constexpr size_t kBatch = 1u << 16;
        std::vector<unsigned long long> macro;
        std::vector<unsigned short> micro;
        std::vector<signed char> channel;
        std::vector<signed char> type;
        if (hint > 0) {
            macro.reserve(static_cast<size_t>(hint));
            micro.reserve(static_cast<size_t>(hint));
            channel.reserve(static_cast<size_t>(hint));
            type.reserve(static_cast<size_t>(hint));
        }

        size_t total = 0;
        for (;;) {
            macro.resize(total + kBatch);
            micro.resize(total + kBatch);
            channel.resize(total + kBatch);
            type.resize(total + kBatch);

            tttrlib_events_v1 events{};
            events.struct_size = sizeof(events);
            events.macro_times = reinterpret_cast<uint64_t*>(macro.data() + total);
            events.micro_times = micro.data() + total;
            events.routing_channels = reinterpret_cast<int8_t*>(channel.data() + total);
            events.event_types = reinterpret_cast<int8_t*>(type.data() + total);
            events.capacity = kBatch;
            events.size = 0;

            const int status = c->read(c->ctx, handle, &events);
            if (status == TTTRLIB_UNSUPPORTED) break;   // "nothing more", not a failure
            if (status != TTTRLIB_OK) return fail("read failed");
            if (events.size == 0) break;
            // A plugin that wrote past what it was given has already corrupted
            // the heap; clamping the count at least keeps the damage out of a
            // length everything downstream trusts.
            total += static_cast<size_t>(std::min<uint64_t>(events.size, kBatch));
        }

        allocate_memory_for_records(total);
        for (size_t i = 0; i < total; ++i) {
            set_macro_time_at(i, macro[i]);
            micro_times[i] = micro[i];
            routing_channels[i] = channel[i];
            event_types[i] = type[i];
        }

        header = new TTTRHeader(container_type);
        if (c->header_json != nullptr) {
            const char* json = nullptr;
            if (c->header_json(c->ctx, handle, &json) == TTTRLIB_OK && json != nullptr) {
                header->set_json(std::string(json));
            }
        }
        header->set_tttr_container_type(container_type);
        c->close(c->ctx, handle);
        handle = nullptr;

        n_records_read = total;
        n_valid_events = total;
        return 1;
    } catch (const std::exception& e) {
        return fail(std::string("threw through the C boundary: ") + e.what());
    } catch (...) {
        return fail("threw through the C boundary");
    }
}


int TTTR::read_flimlabs_file(const char *fn) {
    tttrlib::io::FlimLabsData d;
    try {
        d = tttrlib::io::read_flimlabs(std::string(fn ? fn : ""));
    } catch (const std::exception &e) {
        std::cerr << "Error reading FLIM LABS .bin file: " << e.what() << std::endl;
        return 0;
    }

    const int container = d.flavour == tttrlib::io::FLIMLABS_ITT1
                              ? FL_ITT1_CONTAINER : FL_STT1_CONTAINER;
    header = new TTTRHeader(container);
    header->set_tttr_record_type(d.flavour == tttrlib::io::FLIMLABS_ITT1
                                     ? FL_RECORD_TYPE_ITT1 : FL_RECORD_TYPE_STT1);
    header->set_macro_time_resolution(d.macro_time_resolution_s);
    header->set_number_of_micro_time_channels(d.n_micro_time_channels);
    if (d.micro_time_resolution_s > 0.0) {
        header->set_micro_time_resolution(d.micro_time_resolution_s);
    }
    if (d.laser_period_ns > 0.0) {
        header->set_float_tag("FlimLabs_LaserPeriod_ns", d.laser_period_ns);
    }
    header->set_string_tag("FlimLabs_MacroTimeUnit",
                           d.flavour == tttrlib::io::FLIMLABS_ITT1 ? "picosecond"
                                                                   : "laser_pulse");
    header->set_float_tag("FlimLabs_MacroTimeResidual_ns", d.macro_time_residual_ns);
    if (!d.metadata_json.empty()) {
        header->set_string_tag("FlimLabs_Header", d.metadata_json);
    }
    // Scanner markers, in the container-independent form CLSMImage reads. A
    // FLIM LABS file does not carry the scan geometry, so only the marker
    // channels are known -- the pixel count still has to be supplied.
    header->set_int_tag("ImgHdr_Frame", tttrlib::io::FlimLabsData::MARKER_FRAME);
    header->set_int_tag("ImgHdr_LineStart", tttrlib::io::FlimLabsData::MARKER_LINE);
    header->set_int_tag("ImgHdr_LineStop", tttrlib::io::FlimLabsData::MARKER_LINE);

    const size_t n = d.event_types.size();
    allocate_memory_for_records(n);
    for (size_t i = 0; i < n; ++i) {
        set_macro_time_at(i, static_cast<unsigned long long>(d.macro_times[i]));
        micro_times[i] = d.micro_times[i];
        routing_channels[i] = d.routing_channels[i];
        event_types[i] = d.event_types[i];
    }
    n_records_read = n;
    n_valid_events = n;

    if (d.n_undeclared_channel > 0) {
        std::cerr << "-- WARNING: " << d.n_undeclared_channel
                  << " events are on channels the FLIM LABS header does not list as enabled"
                  << std::endl;
    }
    return 1;
}


int TTTR::read_ps_file(const char *fn) {
    std::string path(fn ? fn : "");

    std::map<std::string, std::vector<int64_t>> streams;
    std::map<std::string, std::string> attrs;
    try {
        streams = photonscore::read_photons(
                path, {"x", "y", "dt", "ms", "channel"});
        attrs = photonscore::read_attributes(path);
    } catch (const std::exception &e) {
        std::cerr << "Error reading .photons file: " << e.what() << std::endl;
        return 0;
    }

    auto attr_long = [&](const char *key, long fallback) -> long {
        auto it = attrs.find(key);
        if (it == attrs.end()) return fallback;
        try { return std::stol(it->second); } catch (...) { return fallback; }
    };
    auto attr_double = [&](const char *key, double fallback) -> double {
        auto it = attrs.find(key);
        if (it == attrs.end()) return fallback;
        try { return std::stod(it->second); } catch (...) { return fallback; }
    };

    int tac_bits = static_cast<int>(attr_long("/photons/TacBits", 12));
    int position_bits = static_cast<int>(attr_long("/photons/PositionBits", 12));
    double tac_channel_ps = attr_double("/photons/TacChannel", 0.0);

    static const std::vector<int64_t> empty;
    const std::vector<int64_t> &xs = streams.count("x") ? streams["x"] : empty;
    const std::vector<int64_t> &ys = streams.count("y") ? streams["y"] : empty;
    const std::vector<int64_t> &dt = streams.count("dt") ? streams["dt"] : empty;
    const std::vector<int64_t> &ms = streams.count("ms") ? streams["ms"] : empty;
    const std::vector<int64_t> &ch = streams.count("channel") ? streams["channel"] : empty;

    // Number of photons: the smallest length among the present core datasets.
    size_t n_photons = SIZE_MAX;
    if (!xs.empty()) n_photons = std::min(n_photons, xs.size());
    if (!ys.empty()) n_photons = std::min(n_photons, ys.size());
    if (!dt.empty()) n_photons = std::min(n_photons, dt.size());
    if (n_photons == SIZE_MAX) n_photons = ms.size();

    // When positions are present, each photon is preceded by two position
    // markers (x, y). This keeps the (x, y) location inside the standard TTTR
    // stream so the existing marker machinery can reconstruct an image without
    // any photonscore-specific code path.
    bool have_positions = !xs.empty() && !ys.empty();
    size_t events_per_photon = have_positions ? 3 : 1;
    size_t n_events = n_photons * events_per_photon;

    n_valid_events = n_events;
    n_records_in_file = n_events;
    n_records_read = n_events;
    allocate_memory_for_records(n_events);

    // Clamp helpers: micro times / positions are 16-bit, routing channels are
    // signed 8-bit. The `flag` is raised only when a value is actually out of
    // range, so callers can warn about real data loss (wide LINCam configs).
    bool clamped_micro = false, clamped_pos = false, clamped_channel = false;
    auto clamp_u16 = [](int64_t v, bool& flag) -> unsigned short {
        if (v < 0) { v = 0; flag = true; }
        else if (v > 65535) { v = 65535; flag = true; }
        return static_cast<unsigned short>(v);
    };
    auto clamp_i8 = [](int64_t v, bool& flag) -> signed char {
        if (v < -128) { v = -128; flag = true; }
        else if (v > 127) { v = 127; flag = true; }
        return static_cast<signed char>(v);
    };

    size_t e = 0;
    for (size_t i = 0; i < n_photons; ++i) {
        unsigned long long mt =
                (i < ms.size()) ? static_cast<unsigned long long>(std::max<int64_t>(0, ms[i])) : 0ULL;
        if (have_positions) {
            // Position marker X (coordinate carried in the micro time)
            set_macro_time_at(e, mt);
            micro_times[e] = clamp_u16(xs[i], clamped_pos);
            routing_channels[e] = MARKER_POSITION_X;
            event_types[e] = RECORD_MARKER;
            ++e;
            // Position marker Y
            set_macro_time_at(e, mt);
            micro_times[e] = clamp_u16(ys[i], clamped_pos);
            routing_channels[e] = MARKER_POSITION_Y;
            event_types[e] = RECORD_MARKER;
            ++e;
        }
        // Photon event
        set_macro_time_at(e, mt);
        micro_times[e] = (i < dt.size()) ? clamp_u16(dt[i], clamped_micro) : 0;
        routing_channels[e] = (i < ch.size()) ? clamp_i8(ch[i], clamped_channel) : 0;
        event_types[e] = RECORD_PHOTON;
        ++e;
    }

    if (clamped_micro || clamped_pos || clamped_channel) {
        std::cerr << "WARNING in TTTR::read_ps_file: values exceeded the TTTR "
                     "field ranges and were clamped (data loss):";
        if (clamped_micro)   std::cerr << " micro time (dt) > 16 bit;";
        if (clamped_pos)     std::cerr << " position (x/y) > 16 bit;";
        if (clamped_channel) std::cerr << " channel outside [-128, 127];";
        std::cerr << std::endl;
    }

    // Header: micro time from the TAC channel calibration, macro time in
    // milliseconds (the /photons/ms marker unit).
    header = new TTTRHeader(PS_PHOTONS_CONTAINER);
    double micro_res_s = (tac_channel_ps > 0.0) ? tac_channel_ps * 1e-12 : 1.0;
    header->set_micro_time_resolution(micro_res_s);
    header->set_macro_time_resolution(1e-3);
    header->set_number_of_micro_time_channels(1 << tac_bits);
    // Record the position range so consumers can bin (x, y) into pixels.
    header->set_float_tag("Photons_PositionBits", position_bits);
    header->set_float_tag("Photons_PositionRange", static_cast<double>(1 << position_bits));

    return 1;
}

bool TTTR::write_ttr_file(const std::string& filename, TTTRHeader* hdr) {
    (void) hdr;   // a .ttr has no header
    try {
        // The channel count decides which routing channels the device could
        // have produced, so the writer needs the same parameters as the reader.
        tttrlib::io::TtrParams params =
                tttrlib::io::ttr_params_from_json(tttr_container_parameters);
        tttrlib::io::write_ttr(filename, macro_times, micro_times,
                               routing_channels, event_types,
                               n_valid_events, params);
    } catch (const std::exception& e) {
        std::cerr << "ERROR in TTTR::write_ttr_file: " << e.what() << std::endl;
        return false;
    }
    return true;
}

bool TTTR::write_ps_file(const std::string& filename, TTTRHeader* hdr) {
    if (hdr == nullptr) hdr = this->header;

    // Reconstruct the datasets from the marker/photon stream. Each photon is
    // preceded by up to two position markers (x, y); the coordinate rides in
    // the marker's micro time (see read_ps_file).
    std::vector<int64_t> xs, ys, dt, ms, ch;
    // At most one photon per valid event; reserve up front so the five parallel
    // dataset buffers do not repeatedly reallocate while scanning the stream.
    xs.reserve(n_valid_events); ys.reserve(n_valid_events);
    dt.reserve(n_valid_events); ms.reserve(n_valid_events);
    ch.reserve(n_valid_events);
    int64_t cur_x = 0, cur_y = 0;
    bool has_x = false, has_y = false;
    bool any_channel = false;
    bool any_position = false;

    for (size_t i = 0; i < n_valid_events; ++i) {
        if (event_types[i] == RECORD_MARKER) {
            if (routing_channels[i] == MARKER_POSITION_X) {
                cur_x = micro_times[i];
                has_x = true;
            } else if (routing_channels[i] == MARKER_POSITION_Y) {
                cur_y = micro_times[i];
                has_y = true;
            }
            continue;
        }
        // Photon event
        dt.push_back(micro_times[i]);
        ms.push_back(static_cast<int64_t>(get_macro_time_at(i)));
        ch.push_back(routing_channels[i]);
        if (routing_channels[i] != 0) any_channel = true;
        // Keep positions aligned one-per-photon; a photon without preceding
        // markers gets a 0 fill so a partially-imaged stream still keeps the
        // positions it does carry (instead of dropping all of them).
        xs.push_back(has_x ? cur_x : 0);
        ys.push_back(has_y ? cur_y : 0);
        if (has_x || has_y) any_position = true;
        has_x = has_y = false;
        cur_x = cur_y = 0;
    }

    // Write positions when at least one photon carried them. xs/ys are aligned
    // one-per-photon, so the length check is always satisfied here.
    bool have_positions = any_position && xs.size() == dt.size() &&
                          ys.size() == dt.size();

    std::vector<photonscore::D7WriteDataset> datasets;
    if (have_positions) {
        datasets.push_back({"/photons/x", 3, std::move(xs)});
        datasets.push_back({"/photons/y", 3, std::move(ys)});
    }
    datasets.push_back({"/photons/dt", 3, std::move(dt)});
    datasets.push_back({"/photons/ms", 3, std::move(ms)});
    if (any_channel) datasets.push_back({"/photons/channel", 3, std::move(ch)});

    // Attributes: reconstruct the calibration from the header.
    std::map<std::string, std::string> attributes;
    double micro_res_s = hdr ? hdr->get_micro_time_resolution() : 0.0;
    if (micro_res_s > 0.0) {
        attributes["/photons/TacChannel"] =
                std::to_string(micro_res_s * 1e12); // seconds -> picoseconds
    }
    int n_micro = hdr ? static_cast<int>(hdr->get_number_of_micro_time_channels()) : 0;
    if (n_micro > 1) {
        int tac_bits = 0;
        while ((1 << tac_bits) < n_micro) ++tac_bits;
        attributes["/photons/TacBits"] = std::to_string(tac_bits);
    }
    if (have_positions) {
        // Position range from the largest coordinate actually stored.
        int64_t max_pos = 0;
        for (const auto& d : datasets) {
            if (d.name == "/photons/x" || d.name == "/photons/y") {
                for (int64_t v : d.values) if (v > max_pos) max_pos = v;
            }
        }
        int pos_bits = 1;
        while ((int64_t(1) << pos_bits) <= max_pos) ++pos_bits;
        if (pos_bits < 12) pos_bits = 12;
        attributes["/photons/PositionBits"] = std::to_string(pos_bits);
    }

    try {
        photonscore::write_photons(filename, datasets, attributes);
    } catch (const std::exception& e) {
        std::cerr << "ERROR in TTTR::write_ps_file: " << e.what() << std::endl;
        return false;
    }
    return true;
}

void TTTR::alex_to_microtime(unsigned long alex_period, int period_shift) {
    for (size_t i = 0; i < n_valid_events; ++i) {
        int64_t m = get_macro_time_at(i) - period_shift;
        micro_times[i] = static_cast<unsigned short>(m % alex_period);
    }
}

void TTTR::read_bh_set_sidecar() {
    // Try to find a .set file with the same base name as the .spc file
    std::string set_filename;
    if (filename.size() >= 4) {
        std::string ext = filename.substr(filename.size() - 4);
        // Safe ASCII-only lowercase transformation (avoids UB with signed char)
        auto to_lower_ascii = [](unsigned char c) -> char {
            return (c >= 'A' && c <= 'Z') ? static_cast<char>(c + ('a' - 'A')) : static_cast<char>(c);
        };
        std::transform(ext.begin(), ext.end(), ext.begin(), to_lower_ascii);
        if (ext == ".spc") {
            set_filename = filename.substr(0, filename.size() - 4) + ".set";
        }
    }
    if (set_filename.empty()) return;

    // Use filesystem to check if file exists (UTF-8 safe)
    std::filesystem::path set_path = std::filesystem::u8path(set_filename);
    if (std::filesystem::exists(set_path)) {
        if (header->read_bh_set_file(set_filename)) {
            if (is_verbose()) {
                std::clog << "-- Parsed BH .set file: " << set_filename << std::endl;
            }
        }
    } else {
        if (is_verbose()) {
            std::clog << "-- BH .set file not found: " << set_filename << std::endl;
        }
    }
}

unsigned TTTR::spcqc_routing_shift() const {
    // The width the routing signal occupies in a decoded routing channel. It is
    // settled once, while the channels are still in their packed form (see
    // compact_spcqc_routing_channels), and stored in the header; everything
    // afterwards -- the record writer above all -- must use that same value,
    // never re-derive it from channels that have since been compacted.
    //
    // Without the tag the data did not come from a QC file (built in memory, or
    // transcoded in). Assume the full width then: it is the only choice that
    // keeps every channel a QC record can express.
    int idx = TTTRHeader::find_tag(header->json_data(), "BH_SPCQC_RoutingBits");
    if (idx < 0) return BH_SPCQC_CH_SHIFT;
    int shift = header->json_data()["tags"][idx]["value"];
    if (shift < 0 || shift > BH_SPCQC_CH_SHIFT) return BH_SPCQC_CH_SHIFT;
    return (unsigned) shift;
}

void TTTR::compact_spcqc_routing_channels() {
    // How many routing bits the measurement declared. Absent means no router.
    int declared = 0;
    int idx = TTTRHeader::find_tag(header->json_data(), "BH_SPCQC_RoutingBits");
    if (idx >= 0) declared = header->json_data()["tags"][idx]["value"];
    if (declared < 0 || declared > BH_SPCQC_CH_SHIFT) declared = BH_SPCQC_CH_SHIFT;

    // Trust but verify: if any photon routes beyond the declared width,
    // compacting would fold two detectors together. Keep all four bits then.
    // Only photons count -- a marker's routing field holds its marker type,
    // which says nothing about how wide the router signal is.
    const unsigned mask = (1u << BH_SPCQC_CH_SHIFT) - 1;
    const unsigned limit = (1u << declared) - 1;
    unsigned shift = (unsigned) declared;
    for (size_t i = 0; i < n_valid_events; i++) {
        if (event_types[i] == RECORD_MARKER) continue;
        if (((unsigned) routing_channels[i] & mask) > limit) {
            if (is_verbose()) {
                std::clog << "-- SPC-QC: routing exceeds the declared "
                          << declared << " bit, keeping the full width" << std::endl;
            }
            shift = BH_SPCQC_CH_SHIFT;
            break;
        }
    }

    // Record what was actually used, so the writer can undo exactly this split
    TTTRHeader::add_tag(header->json_data(), "BH_SPCQC_RoutingBits",
                        (int) shift, tyInt8);
    if (shift == BH_SPCQC_CH_SHIFT) return;  // already in its packed form

    for (size_t i = 0; i < n_valid_events; i++) {
        // A marker's channel is its type, not a detector; leave it alone.
        if (event_types[i] == RECORD_MARKER) continue;
        const unsigned ch = (unsigned) routing_channels[i];
        routing_channels[i] =
                (signed char) ((ch & mask) | ((ch >> BH_SPCQC_CH_SHIFT) << shift));
    }
}

void TTTR::backfill_cz_routing_channels() {
    // Confocor raw data has no channel number in events
    auto tag = header->get_tag(header->json_data(), "channel");
    int channel = tag["value"];
if (is_verbose()) {
    std::clog << "-- Confocor3 channel: " << channel << std::endl;
}
    for(size_t i = 0; i < n_records_in_file; i++) {
        routing_channels[i] = channel;
    }
}

/*!
 * Detects SF-compressed HT3 record streams (Suren Felekyan's HT3
 * conversion). SF files are HT3 files whose overflow records carry the
 * number of additional overflows in their lowest 24 bits; plain HHT3v1
 * overflow records have an all-zero payload. Scans a prefix of the record
 * stream for an overflow record with a non-zero payload. When no such
 * record exists, both interpretations decode identically, so a negative
 * result is always safe.
 *
 * @param fp open file positioned anywhere (position is restored)
 * @param records_begin file offset of the first record
 * @return true if the stream is SF-compressed
 */
static bool detect_sf_ht3_records(std::FILE* fp, size_t records_begin) {
    // SF files carry counted overflow records from the very start of the
    // stream (observed: within the first 3 records); 64k records (256 KB)
    // is a generous margin while keeping the extra read small
    const size_t MAX_SCAN_RECORDS = 65536;
    const size_t CHUNK = 16384;

    int64_t previous_pos = ftell64(fp);
    fseek64(fp, (int64_t) records_begin, SEEK_SET);

    std::vector<uint32_t> buffer(CHUNK);
    size_t scanned = 0;
    bool is_sf = false;
    while (scanned < MAX_SCAN_RECORDS && !is_sf) {
        size_t n = fread(buffer.data(), sizeof(uint32_t), CHUNK, fp);
        if (n == 0) break;
        for (size_t i = 0; i < n; i++) {
            uint32_t rec = buffer[i];
            // overflow record (special=1, channel=0x3F) with non-zero payload
            if (((rec >> 25) == 0x7F) && ((rec & 0xFFFFFF) != 0)) {
                is_sf = true;
                break;
            }
        }
        scanned += n;
    }
    fseek64(fp, previous_pos, SEEK_SET);
    return is_sf;
}

int TTTR::read_records_file(const char *fn, int container_type) {
    fp = open_file(std::string(fn), "rb");
    if (fp == nullptr) return 0;
    header = new TTTRHeader(fp, container_type);

    // BH SPC files may come with a .set sidecar file that holds the settings
    if (container_type == BH_SPC130_CONTAINER ||
        container_type == BH_SPCQC_CONTAINER) {
        read_bh_set_sidecar();
    }

    fp_records_begin = header->end();
    tttr_record_type = header->get_tttr_record_type();

    // HT3 files converted with SF compression are indistinguishable from
    // plain HydraHarp v1 HT3 files by their header; detect them from the
    // record stream (see detect_sf_ht3_records)
    if ((container_type == PQ_HT3_CONTAINER) &&
        (tttr_record_type == PQ_RECORD_TYPE_HHT3v1)) {
        if (detect_sf_ht3_records(fp, fp_records_begin)) {
if (is_verbose()) {
            std::clog << "-- SF-compressed HT3 records detected" << std::endl;
}
            tttr_record_type = PQ_RECORD_TYPE_SF_HT3;
            header->set_tttr_record_type(tttr_record_type);
        }
    }
    n_records_in_file = get_number_of_records_by_file_size(
            fp, header->header_end, header->get_bytes_per_record());
if (is_verbose()) {
    std::clog << "-- TTTR record type: " << tttr_record_type << std::endl;
    std::clog << "-- TTTR number of records: " << n_records_in_file << std::endl;
}
    allocate_memory_for_records(n_records_in_file);
    read_records();
    fclose(fp);

    if (container_type == CZ_CONFOCOR3_CONTAINER) {
        backfill_cz_routing_channels();
    }
    if (container_type == BH_SPCQC_CONTAINER) {
        compact_spcqc_routing_channels();
    }
    return 1;
}

int TTTR::read_file(const char *fn, int container_type) {
if (is_verbose()) {
    std::clog << "READING TTTR FILE" << std::endl;
}
    if(fn == nullptr){
        fn = filename.c_str();
    }
    if(container_type < 0){
        container_type = tttr_container_type;
    }

    // check if file exists (UTF-8 safe)
    std::filesystem::path p = std::filesystem::u8path(fn ? fn : "");
    if (!std::filesystem::exists(p)) {
        std::clog << "-- WARNING: File " << p.u8string() << " does not exist" << std::endl;
        return 0;
    }
if (is_verbose()) {
    std::clog << "-- Filename: " << p.u8string() << std::endl;
}
    // store canonical UTF-8 string version
    this->filename = p.u8string();
    fn = this->filename.c_str();

    // Parameters offered to a container that has none are refused, not
    // dropped. Nearly every format describes itself completely, so a caller
    // passing parameters to one has misunderstood something -- and reading the
    // file anyway hides the misunderstanding behind a plausible result. An
    // empty object is not an offer, so `{}` passes.
    if (tttr_container_parameters.find_first_not_of(" \t\r\n") != std::string::npos) {
        auto parsed = nlohmann::json::parse(tttr_container_parameters, nullptr, false);
        const bool says_something =
                parsed.is_discarded() || !parsed.is_object() || !parsed.empty();
        const auto* fmt = tttrlib::IORegistry::by_container_type(container_type);
        if (says_something && fmt != nullptr && fmt->parameters_schema.empty()) {
            std::cerr << "-- ERROR: container " << fmt->name
                      << " takes no reader parameters, but was given "
                      << tttr_container_parameters << std::endl;
            return 0;
        }
    }

    // Dispatch to the container-specific reader. Photon-HDF5 and SM files
    // have their own file layout; everything else is a header followed by
    // a stream of fixed-size records.
    if (container_type == PHOTON_HDF_CONTAINER) {
        read_hdf_file(fn);
    } else if (container_type == SM_CONTAINER) {
        read_sm_file(fn);
    } else if (container_type == PS_PHOTONS_CONTAINER) {
        read_ps_file(fn);
    } else if (container_type == BE_TTR_CONTAINER) {
        read_ttr_file(fn);
    } else if (container_type == FL_STT1_CONTAINER || container_type == FL_ITT1_CONTAINER) {
        read_flimlabs_file(fn);
    } else if (container_type >= 1000) {
        // Plugin-provided containers are allocated ids from 1000 up; built-in
        // formats own 0-999 permanently, so the range is the dispatch.
        read_plugin_file(fn, container_type);
    } else {
        read_records_file(fn, container_type);
    }

    // A reader allocates for the record count in the file and then finds fewer
    // events, because an overflow or an invalid record is not one. That slack
    // used to be invisible -- every getter reports n_valid_events -- but the
    // event store reports its columns honestly, so a photon table would have
    // shown a tail of zeros with no way to tell them from data. Trimming here
    // costs nothing: the vectors keep their capacity, only their length changes.
    shrink_to_fit();

if (is_verbose()) {
    std::clog << "-- Resulting number of TTTR entries: " << n_valid_events << std::endl;
    if (macro_time_compression_enabled) {
        std::clog << "-- Macro times compressed during read with " << n_keyframes << " keyframes" << std::endl;
    }
}
    return 1;
}


TTTR::~TTTR() {
    delete header;
    deallocate_memory_of_records();
    if (mt_linearizer != nullptr) {
        delete mt_linearizer;
        mt_linearizer = nullptr;
    }
}

std::string TTTR::get_filename() {
    return std::string(filename);
}

/*!
 * \brief Point the raw event arrays at the store's column buffers.
 *
 * Every resize of a column can move its buffer, and every hot loop in the
 * library holds one of these pointers, so this is called after anything that
 * changes a column's size. Nothing here allocates.
 */
void TTTR::sync_event_pointers() {
    auto col = [&](int i) -> tttrlib::data::Column* {
        return i >= 0 ? &events_.column(i) : nullptr;
    };
    tttrlib::data::Column* c;
    macro_times = (c = col(col_macro_time_)) != nullptr ? c->u64_data() : nullptr;
    macro_times_compressed = (c = col(col_macro_delta_)) != nullptr ? c->u32_data() : nullptr;
    micro_times = (c = col(col_micro_time_)) != nullptr ? c->u16_data() : nullptr;
    routing_channels = (c = col(col_routing_channel_)) != nullptr ? c->i8_data() : nullptr;
    event_types = (c = col(col_event_type_)) != nullptr ? c->i8_data() : nullptr;
}

void TTTR::allocate_memory_for_records(size_t n_rec){
if (is_verbose()) {
    std::clog << "-- Allocating memory for " << n_rec << " TTTR records." << std::endl;
}
    // One columnar table, four columns, named. The types are the types the
    // stream is made of -- a routing channel is one byte and a micro time is
    // two -- and widening them would multiply the largest arrays in the library
    // for nothing.
    events_ = tttrlib::data::DataStore();
    // Labelled so it is identifiable in the registry listing. A session ends up
    // holding several of these -- the file, the bursts from it, a localisation
    // table -- and "12 GB in four stores" is only actionable if you can tell
    // which one is which.
    events_.set_label(filename.empty() ? std::string("TTTR") : ("TTTR: " + filename));
    events_.set_n_rows(n_rec);
    col_macro_time_ = -1; col_micro_time_ = -1;
    col_routing_channel_ = -1; col_event_type_ = -1; col_macro_delta_ = -1;

    const bool compress = auto_compress_on_read && macro_time_compression_enabled && n_rec > 0;
    if (compress) {
if (is_verbose()) {
        std::clog << "-- Allocating compressed storage (32-bit deltas + keyframes)" << std::endl;
}
        n_keyframes = (n_rec + keyframe_interval - 1) / keyframe_interval;
        col_macro_delta_ = events_.add_column("macro_time_delta", tttrlib::data::ColumnType::UInt32);
        events_.column(col_macro_delta_).resize_uninitialized(n_rec);
        // The keyframes are not a per-event column -- there is one per
        // keyframe_interval events -- so they stay a plain allocation.
        macro_time_keyframes = (unsigned long long*) malloc(n_keyframes * sizeof(unsigned long long));
        macro_time_compression_enabled = true;
    } else {
        col_macro_time_ = events_.add_column("macro_time", tttrlib::data::ColumnType::UInt64);
        events_.column(col_macro_time_).resize_uninitialized(n_rec);
        macro_time_keyframes = nullptr;
        macro_time_compression_enabled = false;
    }
    col_micro_time_ = events_.add_column("micro_time", tttrlib::data::ColumnType::UInt16);
    col_routing_channel_ = events_.add_column("routing_channel", tttrlib::data::ColumnType::Int8);
    col_event_type_ = events_.add_column("event_type", tttrlib::data::ColumnType::Int8);
    events_.column(col_micro_time_).resize_uninitialized(n_rec);
    events_.column(col_routing_channel_).resize_uninitialized(n_rec);
    events_.column(col_event_type_).resize_uninitialized(n_rec);

    sync_event_pointers();
    capacity = n_rec;

if (is_verbose()) {
    if (compress) {
        size_t compressed_size = n_rec * sizeof(uint32_t) + n_keyframes * sizeof(unsigned long long);
        size_t uncompressed_size = n_rec * sizeof(unsigned long long);
        std::clog << "-- Compressed storage: " << (compressed_size / 1024.0 / 1024.0) << " MB" << std::endl;
        std::clog << "-- vs Uncompressed: " << (uncompressed_size / 1024.0 / 1024.0) << " MB" << std::endl;
        std::clog << "-- Saved: " << ((uncompressed_size - compressed_size) / 1024.0 / 1024.0) << " MB" << std::endl;
    }
}
}

void TTTR::deallocate_memory_of_records(){
    // The event arrays belong to the store and are released with it. Only the
    // keyframes are separately owned, because there is one per interval rather
    // than one per event and they are not a column.
    events_ = tttrlib::data::DataStore();
    col_macro_time_ = -1; col_micro_time_ = -1;
    col_routing_channel_ = -1; col_event_type_ = -1; col_macro_delta_ = -1;
    macro_times = nullptr;
    macro_times_compressed = nullptr;
    micro_times = nullptr;
    routing_channels = nullptr;
    event_types = nullptr;

    if(macro_time_keyframes != nullptr) {
        free(macro_time_keyframes);
        macro_time_keyframes = nullptr;
    }
    capacity = 0;
    n_keyframes = 0;
}

void TTTR::reallocate_memory_for_records(size_t n_rec, bool exact_size){
    // If we already have enough capacity, no need to reallocate
    if(n_rec <= capacity) {
        return;
    }
    
    size_t new_capacity;
    if(exact_size) {
        new_capacity = n_rec;
    } else {
        // Use growth factor of 1.5x to reduce number of reallocations
        // This is a good balance between memory overhead and reallocation frequency
        new_capacity = capacity + capacity / 2;
        if(new_capacity < n_rec) {
            new_capacity = n_rec;
        }
        // Add a minimum growth to avoid too many reallocations for small sizes
        if(new_capacity < capacity + 1024) {
            new_capacity = capacity + 1024;
        }
    }
    
if (is_verbose()) {
    std::clog << "-- Reallocating memory from " << capacity << " to " << new_capacity << " TTTR records." << std::endl;
}
    
    // grow(), not resize(): the existing events have to survive. The store owns
    // the buffers, so this is a vector growth rather than a realloc, and the raw
    // pointers have to be taken again afterwards because a growth can move them.
    for (int c : {col_macro_time_, col_macro_delta_, col_micro_time_,
                  col_routing_channel_, col_event_type_}) {
        if (c >= 0) events_.column(c).grow(new_capacity);
    }
    events_.set_n_rows(new_capacity);
    sync_event_pointers();

    capacity = new_capacity;
}

/*!
 * Runtime-to-compile-time dispatch for record decoding: selects the
 * process_records_batch specialization for a record type.
 * @return false if the record type is unknown
 */
static bool dispatch_process_records_batch(
        int record_type,
        const signed char* buffer,
        size_t num_records,
        size_t bytes_per_record,
        uint64_t& overflow_counter,
        unsigned long long* macro_times,
        unsigned short* micro_times,
        signed char* routing_channels,
        signed char* event_types,
        size_t& valid_count
) {
    #define TTTRLIB_CASE_PROCESS(RT) \
        case RT: process_records_batch<RT>( \
            buffer, num_records, bytes_per_record, overflow_counter, \
            macro_times, micro_times, routing_channels, event_types, \
            valid_count); return true;
    switch(record_type) {
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_PHT3)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_PHT2)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_HHT3v1)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_HHT3v2)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_HHT2v1)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_HHT2v2)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_GENERIC_T3)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_GENERIC_T2)
        TTTRLIB_CASE_PROCESS(PQ_RECORD_TYPE_SF_HT3)
        TTTRLIB_CASE_PROCESS(BH_RECORD_TYPE_SPC130)
        TTTRLIB_CASE_PROCESS(BH_RECORD_TYPE_SPCQC_X04)
        TTTRLIB_CASE_PROCESS(BH_RECORD_TYPE_SPCQC_X06)
        TTTRLIB_CASE_PROCESS(BH_RECORD_TYPE_SPC600_256)
        TTTRLIB_CASE_PROCESS(BH_RECORD_TYPE_SPC600_4096)
        TTTRLIB_CASE_PROCESS(CZ_RECORD_TYPE_CONFOCOR3)
        default:
            std::cerr << "ERROR: Unsupported TTTR record type: " << record_type << std::endl;
            return false;
    }
    #undef TTTRLIB_CASE_PROCESS
}

// Optimized template-dispatched record reading
void TTTR::read_records(
        size_t n_rec,
        bool rewind,
        size_t chunk
) {
    n_rec = n_rec < n_records_in_file ? n_rec : n_records_in_file;
    if(rewind) fseek(fp, (long) fp_records_begin, SEEK_SET);
    
    n_records_read = 0;
    overflow_counter = 0;
    n_valid_events = 0;
    
    size_t bytes_per_record = header->get_bytes_per_record();
    size_t buffer_size = chunk * bytes_per_record;
    auto tmp = (signed char *)malloc(buffer_size);
    if (!tmp) {
        std::cerr << "Memory allocation failed!" << std::endl;
        return;
    }
    
    // Cache pointers - handle both compressed and uncompressed
    unsigned long long* macro_ptr = macro_times;
    unsigned long long* temp_macro_buffer = nullptr;
    uint32_t* macro_compressed_ptr = macro_times_compressed;
    unsigned short* micro_ptr = micro_times;
    signed char* routing_ptr = routing_channels;
    signed char* event_ptr = event_types;
    
    // If compression is enabled, allocate temporary buffer for batch processing
    if (macro_time_compression_enabled) {
        temp_macro_buffer = (unsigned long long*) malloc(chunk * sizeof(unsigned long long));
        if (!temp_macro_buffer) {
            std::cerr << "Memory allocation for temporary macro time buffer failed!" << std::endl;
            free(tmp);
            return;
        }
    }
    
    // Track keyframe state for efficient compression
    unsigned long long current_keyframe_base = 0;
    size_t events_until_next_keyframe = keyframe_interval;
    size_t current_keyframe_idx = 0;
    
    size_t number_of_objects;
    
    // Split into two paths: compressed vs uncompressed for better performance
    if (macro_time_compression_enabled) {
        // COMPRESSED PATH - with on-the-fly compression
        do {
            size_t remaining_records = n_rec - n_records_read;
            size_t adjusted_chunk = remaining_records < chunk ? remaining_records : chunk;
            number_of_objects = fread(tmp, bytes_per_record, adjusted_chunk, fp);
            
            size_t events_before = n_valid_events;
            
            // Use temp buffer for macro times, real arrays for others
            // NOTE: process_records_batch writes at index [valid_count], not [0]
            // So we need to offset the temp buffer pointer by events_before
            unsigned long long* temp_ptr = temp_macro_buffer - events_before;
            
            // Template dispatch based on record type for compile-time optimization
            if (!dispatch_process_records_batch(
                    tttr_record_type,
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events)) {
                free(tmp);
                free(temp_macro_buffer);
                return;
            }
            
            // Compress the macro times we just read
            // Optimized: compute deltas directly, avoiding intermediate absolute time storage
            size_t batch_size = n_valid_events - events_before;
            
            // Data was written to temp_ptr[events_before..n_valid_events-1]
            // which is actually temp_macro_buffer[0..batch_size-1] due to pointer offset
            const unsigned long long* __restrict src = temp_macro_buffer;
            uint32_t* __restrict dst = macro_compressed_ptr + events_before;
            
            // Optimized compression loop - minimal branches, direct delta computation
            for (size_t i = 0; i < batch_size; i++) {
                unsigned long long abs_time = src[i];
                
                // Check if we need a new keyframe (using counter instead of modulo)
                if (events_until_next_keyframe >= keyframe_interval) {
                    macro_time_keyframes[current_keyframe_idx++] = abs_time;
                    current_keyframe_base = abs_time;
                    events_until_next_keyframe = 0;
                    dst[i] = 0;  // First event in keyframe segment has delta=0
                } else {
                    // Store delta from current keyframe
                    dst[i] = (uint32_t)(abs_time - current_keyframe_base);
                }
                
                events_until_next_keyframe++;
            }
            
            n_records_read += number_of_objects;
        } while(number_of_objects > 0);
    } else {
        // UNCOMPRESSED PATH - direct write, no compression overhead
        do {
            size_t remaining_records = n_rec - n_records_read;
            size_t adjusted_chunk = remaining_records < chunk ? remaining_records : chunk;
            number_of_objects = fread(tmp, bytes_per_record, adjusted_chunk, fp);
            
            // Template dispatch based on record type for compile-time optimization
            if (!dispatch_process_records_batch(
                    tttr_record_type,
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events)) {
                free(tmp);
                return;
            }
            
            n_records_read += number_of_objects;
        } while(number_of_objects > 0);
    }
    
    // Free temporary buffers
    if (temp_macro_buffer != nullptr) {
        free(temp_macro_buffer);
    }
    free(tmp);
}

void TTTR::read_records(size_t n_rec){
    // Use 512KB chunks for optimal I/O performance
    // Template dispatch eliminates function pointer overhead
    read_records(n_rec, true, 524288);
}

void TTTR::read_records() {
    read_records(n_records_in_file);
}

TTTRHeader* TTTR::get_header() {
if (is_verbose()) {
    std::clog << "-- TTTR::get_header" << std::endl;
}
    if(header != nullptr){
        return header;
    } else{
        std::clog << "WARNING: TTTR::header not initialized. Returning empty Header." << std::endl;
        header = new TTTRHeader();
        return header;
    }
}

void TTTR::set_header(TTTRHeader* v) {
if (is_verbose()) {
    std::clog << "-- TTTR::set_header" << std::endl;
}
    // If we already own a header and it's not the same pointer, delete it to avoid leaks
    if(header != nullptr && header != v){
        delete header;
        header = nullptr;
    }
    // Assign a copy of the provided header (if any)
    if(v != nullptr){
        header = new TTTRHeader(*v);
    } else {
        // If null is passed, keep header as nullptr
        header = nullptr;
    }
}

void TTTR::get_macro_times(unsigned long long** output, int* n_output){
    if (n_output == nullptr || output == nullptr) {
        throw std::invalid_argument("Output pointers must not be null.");
    }

    // Allocate memory for the output array
    *n_output = static_cast<int>(n_valid_events); // Number of valid events
    *output = (unsigned long long*) malloc(*n_output * sizeof(unsigned long long));

    // Compute shifted macro_times, ensuring no value is less than zero
    // Handle both compressed and uncompressed storage transparently
    if (macro_time_compression_enabled) {
        // Decompress on-the-fly using keyframes
        for (size_t i = 0; i < n_valid_events; ++i) {
            size_t keyframe_idx = i / keyframe_interval;
            unsigned long long keyframe = macro_time_keyframes[keyframe_idx];
            unsigned long long decompressed_time = keyframe + (unsigned long long)macro_times_compressed[i];
            long long shifted_time = static_cast<long long>(decompressed_time) + macro_time_offset;
            (*output)[i] = static_cast<unsigned long long>(std::max(shifted_time, 0LL)); // Clamp to zero
        }
    } else {
        // Direct access to uncompressed data
        for (size_t i = 0; i < n_valid_events; ++i) {
            long long shifted_time = static_cast<long long>(macro_times[i]) + macro_time_offset;
            (*output)[i] = static_cast<unsigned long long>(std::max(shifted_time, 0LL)); // Clamp to zero
        }
    }
}

void TTTR::get_micro_times(unsigned short** output, int* n_output){
    get_array<unsigned short>(n_valid_events, micro_times, output, n_output);
}

void TTTR::get_routing_channel(signed char** output, int* n_output){
    get_array<signed char>(n_valid_events, routing_channels, output, n_output);
}

void TTTR::set_routing_channel(signed char* input, int n_input){
    if (input == nullptr) return;
    if (static_cast<size_t>(n_input < 0 ? 0 : n_input) != n_valid_events)
        throw std::invalid_argument(
            "TTTR::set_routing_channel: got " + std::to_string(n_input) +
            " channels for " + std::to_string(n_valid_events) + " events");
    std::copy(input, input + n_valid_events, routing_channels);
    find_used_routing_channels();
}

void TTTR::get_used_routing_channels(signed char** output, int* n_output){
    get_array<signed char>(
            used_routing_channels.size(),
            used_routing_channels.data(),
            output,
            n_output
    );
}

void TTTR::get_event_type(signed char** output, int* n_output){
    get_array<signed char>(
            n_valid_events,
            event_types,
            output,
            n_output
    );
}

size_t TTTR::get_n_valid_events(){
    return (int) n_valid_events;
}

void TTTR::shrink_to_fit(){
    if(capacity == n_valid_events) {
        // Already at optimal size
        return;
    }
    
    if (is_verbose()) {
        std::clog << "-- Shrinking memory from capacity " << capacity << " to " << n_valid_events << " events." << std::endl;
    }
    
    if(n_valid_events == 0) {
        // Free all memory if no valid events
        deallocate_memory_of_records();
        macro_times = nullptr;
        micro_times = nullptr;
        routing_channels = nullptr;
        event_types = nullptr;
        return;
    }
    
    // A reader allocates for the record count in the file and then finds fewer
    // valid events, because an overflow or an invalid record is not one. truncate
    // keeps the events and drops the tail.
    for (int c : {col_macro_time_, col_macro_delta_, col_micro_time_,
                  col_routing_channel_, col_event_type_}) {
        if (c >= 0) events_.column(c).truncate(n_valid_events);
    }
    events_.set_n_rows(n_valid_events);
    // truncate() shortens without reallocating, so the buffers do not move --
    // but taking the pointers again is free and makes the invariant hold
    // whatever truncate does later.
    sync_event_pointers();

    capacity = n_valid_events;
}

void TTTR::reserve(size_t n){
    if(n <= capacity) {
        // Already have sufficient capacity
        return;
    }
    
    reallocate_memory_for_records(n, true);
}

size_t TTTR::get_n_events(){
    return (int) n_valid_events;
}

void TTTR::get_selection_by_channel(
        int **output, int *n_output,
        signed char *input, int n_input
){
    TTTRMask* m = new TTTRMask();
    m->set_tttr(this);
    m->flip();
    m->select_channels(this, input, n_input);
    auto v = m->get_indices();
    get_array<int>(v.size(), v.data(), output, n_output);
    delete m;
}

TTTR TTTR::operator+(const TTTR* other) const {
    TTTR re;
    re.copy_from(*this, true);
    re.append(other);
    return re;
}

void TTTR::get_selection_by_count_rate(
        int **output, int *n_output,
        double time_window, int n_ph_max,
        bool invert, bool make_mask
){
    // If using compression, extract macro times first
    if (macro_time_compression_enabled) {
        std::vector<unsigned long long> temp_macro_times(n_valid_events);
        for (size_t i = 0; i < n_valid_events; i++) {
            temp_macro_times[i] = get_macro_time_at(i);
        }
        selection_by_count_rate(
                output, n_output,
                temp_macro_times.data(), (int) n_valid_events,
                time_window, n_ph_max,
                header->get_macro_time_resolution(),
                invert, make_mask
        );
    } else {
        selection_by_count_rate(
                output, n_output,
                macro_times, (int) n_valid_events,
                time_window, n_ph_max,
                header->get_macro_time_resolution(),
                invert, make_mask
        );
    }
}

void TTTR::get_time_window_ranges(
        int **output, int *n_output,
        double minimum_window_length,
        int minimum_number_of_photons_in_time_window,
        int maximum_number_of_photons_in_time_window,
        double maximum_window_length,
        double macro_time_calibration,
        bool invert
        ){
    if(macro_time_calibration < 0.0){
        if(header != nullptr){
            macro_time_calibration = header->get_macro_time_resolution();
        } else {
            macro_time_calibration = 1.0;
        }
    }
    // If using compression, extract macro times first
    if (macro_time_compression_enabled) {
        std::vector<unsigned long long> temp_macro_times(n_valid_events);
        for (size_t i = 0; i < n_valid_events; i++) {
            temp_macro_times[i] = get_macro_time_at(i);
        }
        ranges_by_time_window(
                output, n_output,
                temp_macro_times.data(), (int) n_valid_events,
                minimum_window_length, maximum_window_length,
                minimum_number_of_photons_in_time_window,
                maximum_number_of_photons_in_time_window,
                macro_time_calibration,
                invert
        );
    } else {
        ranges_by_time_window(
                output, n_output,
                macro_times, (int) n_valid_events,
                minimum_window_length, maximum_window_length,
                minimum_number_of_photons_in_time_window,
                maximum_number_of_photons_in_time_window,
                macro_time_calibration,
                invert
        );
    }
}

std::shared_ptr<TTTR> TTTR::select(int *selection, int n_selection) {
    return std::make_shared<TTTR>(*this, selection, n_selection);
}


size_t TTTR::get_number_of_records_by_file_size(std::FILE *fp, size_t offset, size_t bytes_per_record){
    size_t n_records_in_file;
    // Use 64-bit file I/O to support files > 2GB on Windows
    // the position of the first record in the file
    int64_t current_position = ftell64(fp);
    if (current_position < 0) {
        if (is_verbose()) {
            std::clog << "-- Error: ftell64 failed to get current position" << std::endl;
        }
        return 0;
    }
    fseek64(fp, 0, SEEK_END);
    int64_t fileSize = ftell64(fp);
    if (fileSize < 0) {
        if (is_verbose()) {
            std::clog << "-- Error: ftell64 failed to get file size" << std::endl;
        }
        return 0;
    }
    // calculate the number of records based on the size of the file
    // and the bytes per record
    if (fileSize < static_cast<int64_t>(offset)) {
        n_records_in_file = 0;
    } else {
        n_records_in_file = static_cast<size_t>(fileSize - static_cast<int64_t>(offset)) / bytes_per_record;
    }
    // move back to the original position
    fseek64(fp, current_position, SEEK_SET);
if (is_verbose()) {
    std::clog << "-- Number of records by file size: " << n_records_in_file << std::endl;
}
    return n_records_in_file;
}


void ranges_by_time_window(
        int **output,
        int *n_output,
        unsigned long long *input,
        int n_input,
        double minimum_window_length,
        double maximum_window_length,
        int minimum_number_of_photons_in_time_window,
        int maximum_number_of_photons_in_time_window,
        double macro_time_calibration,
        bool invert
) {
    // Handle trivial case of no input
    if (n_input <= 0) {
        *output   = nullptr;
        *n_output = 0;
        return;
    }

    // Convert to integer “macro-time” ticks:
    //  - If min length is <= 0, treat as zero  (no lower bound)
    //  - If max length is <= 0, treat as unlimited (use UINT64_MAX)
    uint64_t tw_min = 0;
    if (minimum_window_length > 0.0) {
        tw_min = static_cast<uint64_t>(minimum_window_length / macro_time_calibration);
    }

    uint64_t tw_max = UINT64_MAX;
    bool has_max_tw = false;
    if (maximum_window_length > 0.0) {
        tw_max = static_cast<uint64_t>(maximum_window_length / macro_time_calibration);
        has_max_tw = true;
    }

if (is_verbose()) {
    std::clog << "-- RANGES BY TIME WINDOW " << std::endl;
    std::clog << "-- minimum_window_length [ms]: " << minimum_window_length << std::endl;
    std::clog << "-- maximum_window_length [ms]: " << maximum_window_length << std::endl;
    std::clog << "-- minimum_number_of_photons_in_time_window: "
              << minimum_number_of_photons_in_time_window << std::endl;
    std::clog << "-- maximum_number_of_photons_in_time_window: "
              << maximum_number_of_photons_in_time_window << std::endl;
    std::clog << "-- macro_time_calibration: " << macro_time_calibration << std::endl;
    std::clog << "-- tw_min [macro time clocks]: " << tw_min << std::endl;
    if (has_max_tw) {
        std::clog << "-- tw_max [macro time clocks]: " << tw_max << std::endl;
    } else {
        std::clog << "-- tw_max [macro time clocks]: NONE (no upper limit)" << std::endl;
    }
}

    std::vector<int> ss;
    ss.reserve(200);

    size_t tw_begin = 0;
    while (tw_begin < static_cast<size_t>(n_input)) {

        // search for the first index tw_end where (input[tw_end] - input[tw_begin]) >= tw_min
        // or until the end of the array
        size_t tw_end = tw_begin;
        uint64_t dt   = 0; // difference in macro-time ticks

        for (; tw_end < static_cast<size_t>(n_input); tw_end++) {
            dt = input[tw_end] - input[tw_begin];
            // Break once we've reached or exceeded the "minimum" window length
            if (dt >= tw_min) {
                break;
            }
        }

        // Number of photons in [tw_begin, tw_end)
        // Note that tw_end is the first index that *exceeds* or *equals* tw_min
        size_t n_ph = tw_end - tw_begin;

        // Build the selection logic:
        // 1) If we do have a maximum time window, check dt < tw_max
        //    Otherwise, skip the dt-check
        // 2) If we have a minimum photon threshold (>=0), check n_ph >= that threshold
        // 3) If we have a maximum photon threshold (>=0), check n_ph <= that threshold
        bool pass_time_window = (!has_max_tw || (dt < tw_max));
        bool pass_min_ph      = (minimum_number_of_photons_in_time_window < 0)
                                || (static_cast<int>(n_ph) >= minimum_number_of_photons_in_time_window);
        bool pass_max_ph      = (maximum_number_of_photons_in_time_window < 0)
                                || (static_cast<int>(n_ph) <= maximum_number_of_photons_in_time_window);

        bool is_selected = pass_time_window && pass_min_ph && pass_max_ph;

        // Invert selection if requested
        if (invert) {
            is_selected = !is_selected;
        }

        // If the current segment meets the selection criteria, record [tw_begin, tw_end]
        // - Typically these represent “start index” and “end index”
        if (is_selected) {
            ss.push_back(static_cast<int>(tw_begin));
            ss.push_back(static_cast<int>(tw_end));
        }

        // Move to tw_end
        tw_begin = tw_end;
    }

    // Allocate output array
    *output = static_cast<int*>(std::malloc(ss.size() * sizeof(int)));
    if (!*output) {
        // If allocation fails, signal by returning an empty output
        *n_output = 0;
        return;
    }

    // Copy results to the user-provided pointer
    for (size_t i = 0; i < ss.size(); i++) {
        (*output)[i] = ss[i];
    }
    *n_output = static_cast<int>(ss.size());
}


void selection_by_count_rate(
        int **output,                 // [out] pointer to array of selected indices or mask
        int *n_output,                // [out] number of elements put into *output
        unsigned long long *time,     // [in]  array of time stamps
        int n_time,                   // [in]  number of time stamps
        double time_window,           // [in]  time window in physical units
        int n_ph_max,                 // [in]  photon threshold
        double macro_time_calibration,// [in]  conversion to the same units as time_window
        bool invert,                  // [in]  invert the selection logic
        bool make_mask                // [in]  if not selected, write -1 instead
)
{
    // Edge case: no time stamps => nothing to do
    if (n_time <= 0) {
        *output = nullptr;
        *n_output = 0;
        return;
    }

    // Convert window to integer “ticks” in the same units as 'time[]'
    // safer to cast to unsigned long long since 'time[]' is 64-bit
    unsigned long long tw = static_cast<unsigned long long>(
            time_window / macro_time_calibration
    );

    // Allocate enough room for every index (worst case: we write them all)
    // Correct usage: calloc(# of elements, size of each)
    *output = static_cast<int*>(std::calloc(n_time, sizeof(int)));
    if (!*output) {
        // Allocation failed => return safely
        *n_output = 0;
        return;
    }

    *n_output = 0;
    int i = 0;

    while (i < n_time) {
        // Start a new window at i
        int r = i;
        int n_ph = 0;

        // Advance r while still within 'n_time' and the difference < tw
        // This ensures we never do out-of-bounds access on time[r].
        while (r < n_time && (time[r] - time[i]) < tw) {
            ++r;
            ++n_ph;
        }

        // Decide if this window meets the selection criteria
        bool select = invert ? (n_ph >= n_ph_max) : (n_ph < n_ph_max);

        if (select) {
            // Mark indices [i, r-1] as "selected"
            for (int k = i; k < r; ++k) {
                (*output)[*n_output] = k;
                ++(*n_output);
            }
        }
        else if (make_mask) {
            // Mark indices [i, r-1] as -1
            for (int k = i; k < r; ++k) {
                (*output)[*n_output] = -1;
                ++(*n_output);
            }
        }

        // Move i to the start of the next chunk
        i = r;
    }
}



unsigned int TTTR::get_number_of_micro_time_channels(){
    return header->get_effective_number_of_micro_time_channels();
}

void TTTR::get_intensity_trace(
        int **output, int *n_output,
        double time_window_length
){
    compute_intensity_trace(
            output, n_output,
            this->macro_times, static_cast<int>(this->n_valid_events),
            time_window_length,
            this->header->get_macro_time_resolution()
    );
}

void compute_intensity_trace(
        int               **output,
        int                *n_output,
        unsigned long long *input,
        int                 n_input,
        double              time_window,
        double              macro_time_resolution
) {
    if (n_input <= 0 || time_window <= 0.0 || macro_time_resolution <= 0.0) {
        // Nothing to bin: return a zero‐length “trace” (we still allocate one zeroed int so caller can free()).
        *n_output = 0;
        *output   = (int*)calloc(1, sizeof(int));
        return;
    }

    // 1) How many macro‐clocks fit into one bin?
    //    (time_window is in sec, macro_time_resolution is sec/clock → clocks_per_bin is dimensionless)
    long long clocks_per_bin = (long long) floor(time_window / macro_time_resolution);
    if (clocks_per_bin < 1) {
        // If time_window < macro_time_resolution, force at least 1 clock per bin
        clocks_per_bin = 1;
    }

    // 2) Find the maximum timestamp (in macro‐clocks).
    //    We assume input[] is nondecreasing; if not, you could scan for max instead.
    unsigned long long t_max = input[n_input - 1];

    // 3) How many bins do we need so that the highest event still falls into a valid bin?
    //    If t_max is exactly a multiple of clocks_per_bin,
    //      then (t_max / clocks_per_bin) is an integer, and +1 ensures we have a bin index for it.
    long long n_bins = (t_max / clocks_per_bin) + 1;

    *n_output = (int) n_bins;
    *output = (int*) calloc(n_bins, sizeof(int));
    if (*output == NULL) {
        // Allocation failure—report zero bins.
        *n_output = 0;
        return;
    }

    // 4) Fill the histogram: for each timestamp t, bin = t / clocks_per_bin.
    for (int i = 0; i < n_input; i++) {
        unsigned long long t = input[i];
        long long bin = (long long)(t / clocks_per_bin);
        if (bin < 0) {
            bin = 0;
        } else if (bin >= n_bins) {
            bin = n_bins - 1;
        }
        (*output)[bin]++;
    }
}

// Seems unused
//void get_ranges_channel(
//        unsigned int **ranges, int *n_range,
//        short *channel, int n_channel,
//        int selection_channel
//        ){
//    *n_range = 0;
//    *ranges = (unsigned int *) malloc(2 * n_channel * sizeof(unsigned int));
//
//    int previous_marker_position = 0;
//    int next_marker_position;
//    int i;
//    // find first marker position
//    for(i=0; i<n_channel; i++){
//        if(channel[i] == selection_channel){
//            previous_marker_position = i;
//            break;
//        }
//    }
//    while(i<n_channel){
//        // find next marker position
//        for(; i<n_channel; i++) {
//            if (channel[i] == selection_channel) {
//                next_marker_position = i;
//                *ranges[2 * (*n_range) + 0] = previous_marker_position;
//                *ranges[2 * (*n_range) + 1] = next_marker_position;
//                *n_range += 1;
//                previous_marker_position = next_marker_position;
//                break;
//            }
//        }
//    }
//}


// ============================================================================
// EVENT-STREAM WRITERS
// One writer per record type; each is the inverse of the corresponding
// RecordProcessor<> specialization in TTTRRecordReader.h. Round-trip fidelity
// is defined on the decoded event stream (macro time, micro time, channel,
// event type), not on the raw byte stream.
// ============================================================================

void TTTR::write_spc132_events(FILE* fp, TTTR* tttr){
    bh_overflow_t overflow;
    overflow.allbits = 0;
    overflow.bits.mtov = 1;
    overflow.bits.invalid = 1;

    const uint64_t MT_WRAP = 4096;
    uint64_t MT_ov = 0; // cumulative macro time overflow counter
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        // overflows needed before this event and remaining in-record time
        uint64_t MT_target = MT / MT_WRAP;
        uint64_t MT_ov_needed = MT_target > MT_ov ? MT_target - MT_ov : 0;
        uint64_t dMT = MT % MT_WRAP;
        // write overflow records; each can carry up to 2**28 - 1 overflows
        while (MT_ov_needed > 1) {
            overflow.bits.cnt = (unsigned) std::min((uint64_t) 0x0FFFFFFF, MT_ov_needed);
            fwrite(&overflow, 4, 1, fp);
            MT_ov += overflow.bits.cnt;
            MT_ov_needed -= overflow.bits.cnt;
        }
        bh_spc130_record_t record;
        record.allbits = 0;
        record.bits.mt = (unsigned) dMT;
        record.bits.rout = tttr->routing_channels[n];
        // a single pending overflow is carried by the record's mtov bit
        record.bits.mtov = (MT_ov_needed == 1);
        MT_ov += MT_ov_needed;
        if (tttr->event_types[n] == RECORD_MARKER) {
            // markers: invalid=1, mark=1, marker bits in rout
            record.bits.invalid = 1;
            record.bits.mark = 1;
        } else {
            record.bits.adc = 4095 - std::min<unsigned short>(tttr->micro_times[n], 4095);
        }
        fwrite(&record, 4, 1, fp);
    }
}

void TTTR::write_spcqc_events(FILE* fp, TTTR* tttr, bool six_channel){
    // The QC modules emit one bare overflow word per wrap of the 12 bit macro
    // time field; unlike the classic SPC overflow record there is no count
    // field to compress long idle stretches into a single word.
    const uint32_t overflow = 0x80000000u;

    // Undo the split the reader made: the input channel sits above the routing
    // bits actually in use (see compact_spcqc_routing_channels).
    const unsigned channel_mask = six_channel ? 0x7u : 0x3u;
    const unsigned shift = tttr->spcqc_routing_shift();
    const unsigned routing_mask = (1u << shift) - 1;

    const uint64_t MT_WRAP = BH_SPCQC_MT_WRAP;
    uint64_t MT_ov = 0; // cumulative macro time overflow counter
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        uint64_t MT_target = MT / MT_WRAP;
        for (; MT_ov < MT_target; MT_ov++) {
            fwrite(&overflow, 4, 1, fp);
        }
        bh_spcqc_record_t record;
        record.allbits = 0;
        record.bits.mt = (unsigned) (MT % MT_WRAP);
        if (tttr->event_types[n] == RECORD_MARKER) {
            // Markers carry their type in the routing field; channel and micro
            // time are zero by definition.
            record.bits.rout = (unsigned) (tttr->routing_channels[n] & 0xF);
            record.bits.type = six_channel ? BH_SPCQC_X06_TYPE_MARKER
                                           : (BH_SPCQC_X04_TYPE_MARKER << 2);
        } else {
            const unsigned ch = (unsigned) tttr->routing_channels[n];
            record.bits.rout = ch & routing_mask;
            // photon selector bits are zero, so the channel is the whole field
            record.bits.type = (ch >> shift) & channel_mask;
            // Micro times are stored the way they are histogrammed (no reverse
            // start-stop).
            record.bits.adc = std::min<unsigned short>(
                    tttr->micro_times[n], BH_SPCQC_N_MICRO_TIMES - 1);
        }
        fwrite(&record, 4, 1, fp);
    }
}

void TTTR::write_spc600_256_events(FILE* fp, TTTR* tttr){
    bh_overflow_t overflow;
    overflow.allbits = 0;
    overflow.bits.mtov = 1;
    overflow.bits.invalid = 1;

    // 17-bit macro time field; overflows account for 2**17 units each
    const uint64_t MT_WRAP = 131072;
    uint64_t MT_ov = 0;
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        uint64_t MT_target = MT / MT_WRAP;
        uint64_t MT_ov_needed = MT_target > MT_ov ? MT_target - MT_ov : 0;
        while (MT_ov_needed > 0) {
            overflow.bits.cnt = (unsigned) std::min((uint64_t) 0x0FFFFFFF, MT_ov_needed);
            fwrite(&overflow, 4, 1, fp);
            MT_ov += overflow.bits.cnt;
            MT_ov_needed -= overflow.bits.cnt;
        }
        bh_spc600_256_record_t record;
        record.allbits = 0;
        record.bits.mt = (unsigned) (MT % MT_WRAP);
        record.bits.adc = 255 - std::min<unsigned short>(tttr->micro_times[n], 255);
        record.bits.rout = tttr->routing_channels[n] & 0x7;
        fwrite(&record, 4, 1, fp);
    }
}

void TTTR::write_spc600_4096_events(FILE* fp, TTTR* tttr){
    // 6 bytes per record; each overflow record advances the macro time by 2**24
    const uint64_t MT_WRAP = 16777216;
    const size_t RECORD_SIZE = 6;
    uint64_t MT_ov = 0;
    unsigned char buffer[RECORD_SIZE];
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        uint64_t MT_target = MT / MT_WRAP;
        uint64_t MT_ov_needed = MT_target > MT_ov ? MT_target - MT_ov : 0;
        bh_spc600_4096_record_t record;
        std::memset(&record, 0, sizeof(record));
        record.bits.invalid = 1;
        record.bits.mtov = 1;
        while (MT_ov_needed > 0) {
            std::memcpy(buffer, &record, RECORD_SIZE);
            fwrite(buffer, RECORD_SIZE, 1, fp);
            MT_ov += 1;
            MT_ov_needed -= 1;
        }
        uint64_t dMT = MT % MT_WRAP;
        std::memset(&record, 0, sizeof(record));
        record.bits.mt1 = (dMT >> 0) & 0xFF;
        record.bits.mt2 = (dMT >> 8) & 0xFF;
        record.bits.mt3 = (dMT >> 16) & 0xFF;
        record.bits.adc = 4095 - std::min<unsigned short>(tttr->micro_times[n], 4095);
        record.bits.rout = 255 - tttr->routing_channels[n];
        std::memcpy(buffer, &record, RECORD_SIZE);
        fwrite(buffer, RECORD_SIZE, 1, fp);
    }
}

void TTTR::write_hht3v2_events(FILE* fp, TTTR* tttr){
    const uint64_t T3WRAPAROUND = 1024;
    uint64_t MT_ov = 0;
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        uint64_t MT_target = MT / T3WRAPAROUND;
        uint64_t MT_ov_needed = MT_target > MT_ov ? MT_target - MT_ov : 0;
        // overflow records carry the overflow count in n_sync (10 bit)
        while (MT_ov_needed > 0) {
            pq_hh_t3_record_t rec;
            rec.allbits = 0;
            rec.bits.special = 1;
            rec.bits.channel = 0x3F;
            rec.bits.n_sync = (unsigned) std::min((uint64_t) 1023, MT_ov_needed);
            fwrite(&rec, 4, 1, fp);
            MT_ov += rec.bits.n_sync;
            MT_ov_needed -= rec.bits.n_sync;
        }
        pq_hh_t3_record_t rec;
        rec.allbits = 0;
        rec.bits.special = tttr->event_types[n];
        rec.bits.channel = tttr->routing_channels[n];
        rec.bits.n_sync = (unsigned) (MT % T3WRAPAROUND);
        rec.bits.dtime = tttr->micro_times[n];
        fwrite(&rec, 4, 1, fp);
    }
}

void TTTR::write_hht3v1_events(FILE* fp, TTTR* tttr){
    // HHT3v1: every overflow record advances the macro time by exactly 1024
    const uint64_t T3WRAPAROUND = 1024;
    uint64_t MT_ov = 0;
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        uint64_t MT_target = MT / T3WRAPAROUND;
        uint64_t MT_ov_needed = MT_target > MT_ov ? MT_target - MT_ov : 0;
        while (MT_ov_needed > 0) {
            pq_hh_t3_record_t rec;
            rec.allbits = 0;
            rec.bits.special = 1;
            rec.bits.channel = 0x3F;
            fwrite(&rec, 4, 1, fp);
            MT_ov += 1;
            MT_ov_needed -= 1;
        }
        pq_hh_t3_record_t rec;
        rec.allbits = 0;
        rec.bits.special = tttr->event_types[n];
        rec.bits.channel = tttr->routing_channels[n];
        rec.bits.n_sync = (unsigned) (MT % T3WRAPAROUND);
        rec.bits.dtime = tttr->micro_times[n];
        fwrite(&rec, 4, 1, fp);
    }
}

void TTTR::write_sf_ht3_events(FILE* fp, TTTR* tttr){
    // SF-compressed HT3 (Suren Felekyan's HT3 conversion): photon and
    // marker records as HydraHarp T3; a run of macro time overflows is
    // collapsed into a single overflow record whose lowest 24 bits hold
    // the number of ADDITIONAL overflows (record advances the sync counter
    // by (1 + count) * 1024).
    const uint64_t T3WRAPAROUND = 1024;
    const uint64_t MAX_PER_RECORD = 0x1000000; // 1 + 24-bit count
    uint64_t MT_ov = 0;
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        uint64_t MT_target = MT / T3WRAPAROUND;
        uint64_t MT_ov_needed = MT_target > MT_ov ? MT_target - MT_ov : 0;
        while (MT_ov_needed > 0) {
            uint64_t count = std::min(MAX_PER_RECORD, MT_ov_needed);
            // special=1, channel=0x3F, payload = count - 1
            uint32_t rec = 0xFE000000u | (uint32_t) (count - 1);
            fwrite(&rec, 4, 1, fp);
            MT_ov += count;
            MT_ov_needed -= count;
        }
        pq_hh_t3_record_t rec;
        rec.allbits = 0;
        rec.bits.special = tttr->event_types[n];
        rec.bits.channel = tttr->routing_channels[n];
        rec.bits.n_sync = (unsigned) (MT % T3WRAPAROUND);
        rec.bits.dtime = tttr->micro_times[n];
        fwrite(&rec, 4, 1, fp);
    }
}

void TTTR::write_hht2v2_events(FILE* fp, TTTR* tttr){
    // T2 records carry no micro time; micro times are dropped
    const uint64_t T2WRAPAROUND_V2 = 33554432;
    uint64_t MT_ov = 0;
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        uint64_t MT_target = MT / T2WRAPAROUND_V2;
        uint64_t MT_ov_needed = MT_target > MT_ov ? MT_target - MT_ov : 0;
        // overflow records carry the overflow count in timetag (25 bit)
        while (MT_ov_needed > 0) {
            pq_hh_t2_record_t rec;
            rec.allbits = 0;
            rec.bits.special = 1;
            rec.bits.channel = 0x3F;
            rec.bits.timetag = (unsigned) std::min((uint64_t) 0x1FFFFFF, MT_ov_needed);
            fwrite(&rec, 4, 1, fp);
            MT_ov += rec.bits.timetag;
            MT_ov_needed -= rec.bits.timetag;
        }
        pq_hh_t2_record_t rec;
        rec.allbits = 0;
        rec.bits.special = tttr->event_types[n];
        rec.bits.channel = tttr->routing_channels[n];
        rec.bits.timetag = (unsigned) (MT % T2WRAPAROUND_V2);
        fwrite(&rec, 4, 1, fp);
    }
}

void TTTR::write_hht2v1_events(FILE* fp, TTTR* tttr){
    // HHT2v1: every overflow record advances the time tag by exactly 33552000
    const uint64_t T2WRAPAROUND_V1 = 33552000;
    uint64_t MT_ov = 0;
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        uint64_t MT_target = MT / T2WRAPAROUND_V1;
        uint64_t MT_ov_needed = MT_target > MT_ov ? MT_target - MT_ov : 0;
        while (MT_ov_needed > 0) {
            pq_hh_t2_record_t rec;
            rec.allbits = 0;
            rec.bits.special = 1;
            rec.bits.channel = 0x3F;
            fwrite(&rec, 4, 1, fp);
            MT_ov += 1;
            MT_ov_needed -= 1;
        }
        pq_hh_t2_record_t rec;
        rec.allbits = 0;
        rec.bits.special = tttr->event_types[n];
        rec.bits.channel = tttr->routing_channels[n];
        rec.bits.timetag = (unsigned) (MT % T2WRAPAROUND_V1);
        fwrite(&rec, 4, 1, fp);
    }
}

void TTTR::write_pht3_events(FILE* fp, TTTR* tttr){
    // PicoHarp T3. Markers are encoded with dtime = 0 (PicoHarp convention);
    // photons therefore need dtime >= 1: micro time 0 is clipped to 1.
    const uint64_t T3WRAPAROUND = 65536;
    uint64_t MT_ov = 0;
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        uint64_t MT_target = MT / T3WRAPAROUND;
        uint64_t MT_ov_needed = MT_target > MT_ov ? MT_target - MT_ov : 0;
        // overflow record: channel 0xF and dtime 0, advances by 65536
        while (MT_ov_needed > 0) {
            pq_ph_t3_record_t rec;
            rec.allbits = 0;
            rec.bits.channel = 0xF;
            fwrite(&rec, 4, 1, fp);
            MT_ov += 1;
            MT_ov_needed -= 1;
        }
        pq_ph_t3_record_t rec;
        rec.allbits = 0;
        rec.bits.channel = tttr->routing_channels[n] & 0xF;
        rec.bits.n_sync = (unsigned) (MT % T3WRAPAROUND);
        if (tttr->event_types[n] == RECORD_MARKER) {
            rec.bits.dtime = 0;
        } else {
            rec.bits.dtime = std::max<unsigned short>(
                    1, std::min<unsigned short>(tttr->micro_times[n], 4095));
        }
        fwrite(&rec, 4, 1, fp);
    }
}

void TTTR::write_pht2_events(FILE* fp, TTTR* tttr){
    // PicoHarp T2; no micro time. Markers use channel 0xF with the marker
    // bits in the lowest 4 bits of the time tag (time tag loses 4 bits).
    const uint64_t T2WRAPAROUND = 210698240;
    uint64_t MT_ov = 0;
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        uint64_t MT_target = MT / T2WRAPAROUND;
        uint64_t MT_ov_needed = MT_target > MT_ov ? MT_target - MT_ov : 0;
        // overflow record: channel 0xF, marker bits zero
        while (MT_ov_needed > 0) {
            pq_ph_t2_record_t rec;
            rec.allbits = 0;
            rec.bits.channel = 0xF;
            fwrite(&rec, 4, 1, fp);
            MT_ov += 1;
            MT_ov_needed -= 1;
        }
        pq_ph_t2_record_t rec;
        rec.allbits = 0;
        uint64_t dMT = MT % T2WRAPAROUND;
        if (tttr->event_types[n] == RECORD_MARKER) {
            rec.bits.channel = 0xF;
            rec.bits.time = (unsigned) ((dMT & ~0xFULL) | (tttr->routing_channels[n] & 0xF));
        } else {
            rec.bits.channel = tttr->routing_channels[n] & 0xF;
            rec.bits.time = (unsigned) dMT;
        }
        fwrite(&rec, 4, 1, fp);
    }
}

void TTTR::write_cz_events(FILE* fp, TTTR* tttr){
    // CZ ConfoCor3 raw records store 32-bit macro time deltas; micro times,
    // channel numbers (header carries a single channel) and event types drop.
    uint64_t previous = 0;
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        cz_confocor3_raw_record_t rec;
        rec.allbits = (uint32_t) (MT - previous);
        previous = MT;
        fwrite(&rec, 4, 1, fp);
    }
}

void TTTR::write_sm_events(FILE* fp, TTTR* tttr){
    // SM records: 8-byte big-endian macro time + 4-byte big-endian channel.
    // Micro times drop. The file ends with a 26-byte trailer.
    for (size_t n = 0; n < tttr->size(); n++) {
        uint64_t MT = tttr->get_macro_time_at(n);
        unsigned char rec[12];
        for (int b = 0; b < 8; b++) rec[b] = (MT >> (8 * (7 - b))) & 0xFF;
        uint32_t channel = (uint32_t) tttr->routing_channels[n];
        for (int b = 0; b < 4; b++) rec[8 + b] = (channel >> (8 * (3 - b))) & 0xFF;
        fwrite(rec, sizeof(rec), 1, fp);
    }
    unsigned char trailer[26];
    std::memset(trailer, 0, sizeof(trailer));
    fwrite(trailer, sizeof(trailer), 1, fp);
}

void update_ptu_header(FILE* fpin, char Ident[32], uint64_t TagValue){
    tag_head_t TagHead;
    do {
        fread(&TagHead, 1, sizeof(TagHead), fpin);
        if(TagHead.Ident == Ident){
            TagHead.TagValue = TagValue;
            fseek(fpin, (ftell(fpin) - sizeof(TagHead)), SEEK_CUR);
        }
    } while ((strncmp(TagHead.Ident, FileTagEnd.c_str(), sizeof(FileTagEnd))) != 0);
}

void TTTR::write_header(std::string &fn, TTTRHeader* header){
    if(header == nullptr)
        header = this->header;
    int container_type = header->get_tttr_container_type();
    if(container_type < 0)
        container_type = this->tttr_container_type;
    if(container_type == BH_SPC130_CONTAINER){
        TTTRHeader::write_spc132_header(fn, header);
    } else if(container_type == BH_SPCQC_CONTAINER){
        TTTRHeader::write_spcqc_header(fn, header);
    } else if(container_type == PQ_PTU_CONTAINER){
        TTTRHeader::write_ptu_header(fn, header);
    } else if(container_type == PQ_HT3_CONTAINER){
        TTTRHeader::write_ht3_header(fn, header);
    } else if(container_type == SM_CONTAINER){
        TTTRHeader::write_sm_header(fn, header);
    } else if(container_type == CZ_CONFOCOR3_CONTAINER){
        TTTRHeader::write_cz_confocor3_header(fn, header);
    } else if(
            (container_type == BH_SPC600_256_CONTAINER) ||
            (container_type == BH_SPC600_4096_CONTAINER)){
        // SPC-600 files have no on-disk header; create/truncate the file
        FILE* f = fopen(fn.c_str(), "wb");
        if (f != nullptr) fclose(f);
    } else{
        std::cerr << "Error in TTTR::write, writing of headers not implemented" << std::endl;
    }
}

/*!
 * Checks if a combination of container and record makes sense
 * @param container_type
 * @param record_type
 * @return
 */
bool valid_container_record_pair(int container_type, int record_type){
    // Was a 35-line if/else chain restating what the format table already says.
    // Photon-HDF5 stores decoded arrays rather than records, so it accepts any
    // record type -- expressed as an empty record_types list.
    const auto* f = tttrlib::IORegistry::by_container_type(container_type);
    return f != nullptr && f->accepts_record_type(record_type);
}

/*!
 * Canonical record type used when transcoding into a container whose header
 * does not carry a (valid) record type for it. -1 when the format does not need
 * one, which is true of Photon-HDF5 and of Photonscore.
 */
static int default_record_type_for_container(int container_type){
    const auto* f = tttrlib::IORegistry::by_container_type(container_type);
    return f ? f->default_record_type : -1;
}

/*!
 * Maps a tttrlib record type to the PicoQuant TTResultFormat_TTTRRecType
 * identifier written into PTU headers.
 */
static int pq_ptu_record_type_identifier(int record_type){
    switch (record_type) {
        case PQ_RECORD_TYPE_PHT3:       return rtPicoHarpT3;
        case PQ_RECORD_TYPE_PHT2:       return rtPicoHarpT2;
        case PQ_RECORD_TYPE_HHT3v1:     return rtHydraHarpT3;
        case PQ_RECORD_TYPE_HHT2v1:     return rtHydraHarpT2;
        case PQ_RECORD_TYPE_HHT3v2:     return rtHydraHarp2T3;
        case PQ_RECORD_TYPE_HHT2v2:     return rtHydraHarp2T2;
        case PQ_RECORD_TYPE_GENERIC_T3: return rtMultiHarpT3;
        case PQ_RECORD_TYPE_GENERIC_T2: return rtMultiHarpT2;
        default: return -1;
    }
}

bool TTTR::write(std::string filename, const char* container_type, TTTRHeader* header){
    int ct = -1;
    if(container_type != nullptr){
        std::string name(container_type);
        if(container_names().count_left(name)){
            ct = container_names().left.at(name);
        } else {
            std::cerr << "ERROR in TTTR::write: unknown container type '"
                      << name << "'." << std::endl;
            return false;
        }
    }
    return write(filename, header, ct);
}

bool TTTR::write(std::string filename, TTTRHeader* header, int container_type){
    if(header == nullptr) header = this->header;

    // Writing must not change the object being written.
    //
    // Further down, the target container and record type are stamped into the
    // header and any missing tags are filled in. Doing that to `this->header`
    // leaves the object describing the file just written, so a second write to
    // a different container starts from the wrong description -- and the result
    // is not an error but a silently corrupt file. Writing HT3 after PTU
    // emitted HHT3v2 records under a header the reader then resolved as
    // SF-compressed, so every macro time after the first overflow was expanded
    // with the wrong rule. The event count matched, which is exactly what makes
    // it worth guarding against.
    TTTRHeader header_copy(*header);
    header = &header_copy;

    // Determine the container type already associated with the data.
    int source_type = header->get_tttr_container_type();
    if(source_type < 0) source_type = this->tttr_container_type;
    // Choose the output container:
    //  1. An explicit container_type argument always wins.
    //  2. Otherwise the filename extension selects the format, but only when
    //     it names a different format family than the source. A generic
    //     extension (e.g. ".spc", shared by all Becker & Hickl SPC flavours)
    //     that matches the source family keeps the more specific source type,
    //     so same-format round trips preserve SPC-600 vs SPC-130 etc.
    //  3. Unknown extensions fall back to the source container type.
    if(container_type < 0){
        int ext_type = inferTTTRContainerTypeFromExtension(filename);
        if(ext_type < 0){
            container_type = source_type;
        } else if(source_type >= 0 &&
                  tttrContainerCanonicalExtension(source_type) ==
                  tttrContainerCanonicalExtension(ext_type)){
            container_type = source_type;
        } else {
            container_type = ext_type;
        }
    }
    // Photonscore ".photons" (D7) has its own file layout (no header + record
    // stream) and no per-record-type writer; reconstruct the position/photon
    // datasets from the marker stream and write a D7 container.
    if(container_type == PS_PHOTONS_CONTAINER){
        return write_ps_file(filename, header);
    }
    // BrightEyes-TTM ".ttr" is a bare word stream with no header at all, so it
    // shares nothing with the header + records path below.
    if(container_type == BE_TTR_CONTAINER){
        return write_ttr_file(filename, header);
    }
    // FLIM LABS is read-only. Writing one means baking in the choice of what a
    // macro time tick is (see io_fl.h) and emitting it as if the instrument
    // had; that choice has never been checked against a file the instrument
    // wrote, because no such file is published. See PRD-012.
    if(container_type == FL_STT1_CONTAINER || container_type == FL_ITT1_CONTAINER){
        std::cerr << "ERROR in TTTR::write: tttrlib reads FLIM LABS time-tagger files "
                     "but does not write them." << std::endl;
        return false;
    }

    int record_type = header->get_tttr_record_type();
    // Transcoding: fall back to the container's canonical record type when
    // the header's record type does not fit the target container.
    if(!valid_container_record_pair(container_type, record_type)){
        record_type = default_record_type_for_container(container_type);
    }
    if(!valid_container_record_pair(container_type, record_type)){
        std::cerr << "ERROR in TTTR::write: invalid container record combination." << std::endl;
        return false;
    }

    // Photon-HDF5 has its own file layout (no header + record stream)
    if(container_type == PHOTON_HDF_CONTAINER){
        return write_hdf_file(filename, header);
    }

    // Keep the header metadata consistent with the records actually written,
    // so the file reads back with the correct record decoder.
    header->set_tttr_container_type(container_type);
    header->set_tttr_record_type(record_type);
    // Fill in any metadata the target container needs but the (possibly
    // transcoded or freshly built) header is missing, without clobbering
    // metadata that already survived from the source.
    TTTRHeader::ensure_minimal_tags(header, container_type, get_n_valid_events());
    if(container_type == PQ_PTU_CONTAINER){
        TTTRHeader::add_tag(
                header->json_data(), TTTRTagTTTRRecType,
                pq_ptu_record_type_identifier(record_type), tyInt8);
    }

    write_header(filename, header);
    fp = open_file(filename, "ab");
    if (fp == nullptr) {
        std::cerr << "ERROR: Cannot write to file: " << filename << std::endl;
        return false;
    }
    // append records
    switch (record_type) {
        case BH_RECORD_TYPE_SPC130:
            write_spc132_events(fp, this);
            break;
        case BH_RECORD_TYPE_SPCQC_X04:
            write_spcqc_events(fp, this, false);
            break;
        case BH_RECORD_TYPE_SPCQC_X06:
            write_spcqc_events(fp, this, true);
            break;
        case BH_RECORD_TYPE_SPC600_256:
            write_spc600_256_events(fp, this);
            break;
        case BH_RECORD_TYPE_SPC600_4096:
            write_spc600_4096_events(fp, this);
            break;
        case PQ_RECORD_TYPE_HHT3v2:
        case PQ_RECORD_TYPE_GENERIC_T3:
            write_hht3v2_events(fp, this);
            break;
        case PQ_RECORD_TYPE_HHT3v1:
            write_hht3v1_events(fp, this);
            break;
        case PQ_RECORD_TYPE_SF_HT3:
            write_sf_ht3_events(fp, this);
            break;
        case PQ_RECORD_TYPE_HHT2v2:
        case PQ_RECORD_TYPE_GENERIC_T2:
            write_hht2v2_events(fp, this);
            break;
        case PQ_RECORD_TYPE_HHT2v1:
            write_hht2v1_events(fp, this);
            break;
        case PQ_RECORD_TYPE_PHT3:
            write_pht3_events(fp, this);
            break;
        case PQ_RECORD_TYPE_PHT2:
            write_pht2_events(fp, this);
            break;
        case CZ_RECORD_TYPE_CONFOCOR3:
            write_cz_events(fp, this);
            break;
        case SM_RECORD_TYPE:
            write_sm_events(fp, this);
            break;
        default:
            std::cerr << "ERROR: Record type " << record_type << " not supported" << std::endl;
            fclose(fp);
            return false;
    }
    fclose(fp);
    // For Becker & Hickl SPC files, write the companion .set sidecar carrying
    // the CLSM imaging geometry that the .spc record stream cannot hold, so a
    // PTU -> SPC conversion of imaging data stays reconstructable (the reader
    // picks up the .set automatically, see read_bh_set_sidecar).
    if(container_type == BH_SPC130_CONTAINER ||
       container_type == BH_SPC600_256_CONTAINER ||
       container_type == BH_SPC600_4096_CONTAINER ||
       container_type == BH_SPCQC_CONTAINER){
        auto dot = filename.rfind('.');
        std::string set_fn =
            (dot == std::string::npos ? filename : filename.substr(0, dot)) + ".set";
        TTTRHeader::write_bh_set_file(set_fn, header);
    }
    return true;
}


TTTR& TTTR::operator%(unsigned short mod_value) {
    if (mod_value == 0) {
        throw std::invalid_argument("Modulo by zero is undefined.");
    }
    int n_mt = header->get_effective_number_of_micro_time_channels();
    for (size_t i = 0; i < n_valid_events; ++i) {
        micro_times[i] = static_cast<unsigned short>(
                (static_cast<int>(micro_times[i]) - mod_value + n_mt) % n_mt
        );
    }

    return *this; // Allow chaining
}


TTTR& TTTR::operator<<(long long offset) {
    // Update macro time offset cache
    macro_time_offset += offset;
    // Do not alter the actual `macro_times` array
    return *this; // Allow chaining
}


void TTTR::shift_micro_time_by_channel(signed char channel, unsigned short shift_value) {
    // Get the number of micro-time bins per macro-tick
    unsigned int n_mt = header->get_effective_number_of_micro_time_channels();

    // Avoid division by zero
    if (n_mt == 0) return;

    // For each valid event, if it’s on the requested channel, shift & wrap
    for (size_t i = 0; i < n_valid_events; ++i) {
        if (routing_channels[i] == channel) {
            // Add shift, then wrap within [0, n_mt)
            unsigned int new_mt = static_cast<unsigned int>(micro_times[i]) + shift_value;
            micro_times[i] = static_cast<unsigned short>(new_mt % n_mt);
        }
    }
}


void TTTR::compute_microtime_histogram(
        TTTR *tttr_data,
        double** histogram, int* n_histogram,
        double** time, int* n_time,
        unsigned short micro_time_coarsening,
        std::vector<int> *tttr_indices,
        std::vector<int> *routing_channels,
        int minlength
) {
    // Validate pointers
    if (!tttr_data || !histogram || !n_histogram || !time || !n_time) {
        return;
    }

    // Prevent zero coarsening
    if (micro_time_coarsening == 0) micro_time_coarsening = 1;

    // minlength == -2 is a sentinel that clips the histogram to one excitation
    // period (see below) rather than acting as a minimum length.
    const bool limit_to_repetition_period = (minlength == -2);

    // Default resolution and base channel count
    double resolution = 1.0;
    int base_channels = 0;

    auto header_ptr = tttr_data->get_header();
    if (header_ptr) {
        auto header = *header_ptr;
        double hdr_res = header.get_micro_time_resolution();
        resolution = (hdr_res > 0 ? hdr_res : 1.0);
        if (limit_to_repetition_period) {
            // Clip the histogram to the channels that fit within one excitation
            // period: floor((1/rep_rate) / micro_time_resolution). Fall back to
            // the total TAC channel count when the header lacks the rep-rate /
            // global-resolution info (effective count would be 0).
            unsigned int eff = header.get_effective_number_of_micro_time_channels();
            base_channels = (eff > 0) ? (int) eff
                                      : header.get_number_of_micro_time_channels();
        } else {
            base_channels = header.get_number_of_micro_time_channels();
        }
    }

    // Determine number of bins
    int raw_bins = base_channels / micro_time_coarsening;
    int n_channels;
    if (minlength < 0) {
        // use original behavior when minlength < 0
        n_channels = std::max(1, raw_bins);
    } else {
        // enforce minimum length
        n_channels = std::max(minlength, raw_bins);
    }

    // Build time axis
    std::vector<double> t_vec(n_channels);
    for (int i = 0; i < n_channels; ++i) {
        t_vec[i] = resolution * i * micro_time_coarsening;
    }

    // Prepare histogram container
    std::vector<double> hist_vec(n_channels, 0.0);

    // Channel lookup table for O(1) membership tests instead of a per-photon
    // std::find over the channel list. Routing channels are signed char, so
    // only list values in [-128, 127] can ever match.
    bool chan_ok[256];
    const bool filter_channels = (routing_channels != nullptr);
    if (filter_channels) {
        std::memset(chan_ok, 0, sizeof(chan_ok));
        for (int v : *routing_channels) {
            if (v >= -128 && v <= 127) {
                chan_ok[static_cast<unsigned char>(static_cast<signed char>(v))] = true;
            }
        }
    }

    // Collect micro-time values
    std::vector<unsigned short> selected;
    if (!tttr_indices) {
        unsigned short* micro_times = nullptr;
        int n_micro_times = 0;
        tttr_data->get_micro_times(&micro_times, &n_micro_times);
        if (micro_times && n_micro_times > 0) {
            selected.reserve(n_micro_times);
            for (int i = 0; i < n_micro_times; ++i) {
                if (!filter_channels ||
                    chan_ok[static_cast<unsigned char>(tttr_data->routing_channels[i])]) {
                    selected.push_back(micro_times[i] / micro_time_coarsening);
                }
            }
        }
    } else {
        selected.reserve(tttr_indices->size());
        for (int idx : *tttr_indices) {
            if (!filter_channels ||
                chan_ok[static_cast<unsigned char>(tttr_data->routing_channels[idx])]) {
                selected.push_back(tttr_data->micro_times[idx] / micro_time_coarsening);
            }
        }
    }

    // Handle empty data when using old behavior (minlength < 0)
    if (selected.empty() && minlength < 0) {
        // return empty arrays
        *n_histogram = *n_time = 0;
        *histogram = (double*) calloc(0, sizeof(double));
        *time      = (double*) calloc(0, sizeof(double));
        return;
    }

    // Create bin edges
    std::vector<unsigned short> bin_edges(n_channels);
    std::iota(bin_edges.begin(), bin_edges.end(), 0);

    // Compute histogram only if data present
    if (!selected.empty()) {
        histogram1D<unsigned short>(
                selected.data(), static_cast<int>(selected.size()),
                nullptr, 0,
                bin_edges.data(), static_cast<int>(bin_edges.size()),
                hist_vec.data(), n_channels,
                "lin", false
        );
    }

    // Allocate output arrays
    *histogram = static_cast<double*>(std::malloc(n_channels * sizeof(double)));
    *time      = static_cast<double*>(std::malloc(n_channels * sizeof(double)));
    if (!*histogram || !*time) {
        std::free(*histogram);
        std::free(*time);
        *n_histogram = *n_time = 0;
        *histogram = (double*) calloc(1, sizeof(double));
        *time      = (double*) calloc(1, sizeof(double));
        return;
    }

    // Copy data into output
    std::copy(hist_vec.begin(), hist_vec.end(), *histogram);
    std::copy(t_vec.begin(), t_vec.end(), *time);

    *n_histogram = *n_time = n_channels;
}



double TTTR::compute_mean_lifetime(
        TTTR* tttr_data,
        TTTR* tttr_irf,
        double m0_irf, double m1_irf,
        std::vector<int> *tttr_indices,
        double dt,
        int minimum_number_of_photons,
        std::vector<double> *background,
        double m0_bg, double m1_bg, 
        double background_fraction
){
    if(dt < 0.0){
        dt = tttr_data->header->get_micro_time_resolution();
    }

    // Compute moments for IRF
    if(tttr_irf != nullptr){
        // number of photons
        m0_irf = (double) tttr_irf->n_valid_events;
        // sum of photon arrival times
        m1_irf = (double) std::accumulate(
                tttr_irf->micro_times,
                tttr_irf->micro_times + tttr_irf->n_valid_events, 0.0);
    }

    // Compute moments for background pattern
    if(background != nullptr){
        m0_bg = 0.0; m1_bg = 0.0;
        for(size_t i = 0; i < background->size(); i++){
            m0_bg += (*background)[i];
            m1_bg += i * (*background)[i];
        }
    }

    // Compute moments for data
    double m0_h = 0.0; // total number of photons
    double m1_h = 0.0; // sum of photon arrival times
    if(tttr_indices == nullptr){
        m0_h += (double) tttr_data->n_valid_events;
        for(size_t i = 0; i < tttr_data->n_valid_events; i++)
            m1_h += tttr_data->micro_times[i];
    } else{
        m0_h += (double) tttr_indices->size();
        for (auto &vi: *tttr_indices)
            m1_h += tttr_data->micro_times[vi];
    }

    // Scale by background fraction
    if(background_fraction > 0.0){
        m1_bg = m1_bg * (m0_h / m0_bg) * background_fraction;
        m0_bg = m0_h * background_fraction;
    }

    // Compute average lifetime
    double lt = 0.0;
    if (m0_h > minimum_number_of_photons) {
        lt =  (m1_h - m1_bg) / (m0_h - m0_bg) - m1_irf / m0_irf;
        lt *= dt;
    }

    return lt;
}


void TTTR::append_events(
        unsigned long long *macro_times, int n_macrotimes,
        unsigned short *micro_times, int n_microtimes,
        signed char *routing_channels, int n_routing_channels,
        signed char *event_types, int n_event_types,
        bool shift_macro_time,
        long long macro_time_offset
){
    
    if(
        (n_macrotimes == n_microtimes) &&
        (n_microtimes == n_routing_channels) &&
        (n_routing_channels == n_event_types)
    ){
        
if (is_verbose()) {
        std::cout << "-- Appending number of records: " << n_macrotimes << std::endl;
}
        // Decompress if compressed (appending requires uncompressed storage)
        if (macro_time_compression_enabled) {
            decompress_macro_times();
        }
        
        size_t n_rec = this->n_valid_events + n_macrotimes;
        // Use reallocate_memory_for_records with growth factor for better performance
        reallocate_memory_for_records(n_rec, false);
        
        
        if(n_valid_events > 0){
            if(shift_macro_time){
                macro_time_offset += get_macro_time_at(n_valid_events - 1);
            }
        }
        
        for(int i_rec=0; i_rec < n_macrotimes; i_rec++){
            set_macro_time_at(i_rec + n_valid_events, macro_times[i_rec] + macro_time_offset);
            this->micro_times[i_rec + n_valid_events] = micro_times[i_rec];
            this->routing_channels[i_rec + n_valid_events] = routing_channels[i_rec];
            this->event_types[i_rec + n_valid_events] = event_types[i_rec];
        }
        n_valid_events += n_macrotimes;
    } else{
        std::cerr << "ERROR: Cannot append events the length of input arrays differ." << std::endl;
    }
}


void TTTR::append(
        const TTTR *other,
        bool shift_macro_time,
        long long macro_time_offset
){
    // If other is using compression, we need to decompress first
    if (other->macro_time_compression_enabled) {
        // Extract macro times into a temporary buffer
        std::vector<unsigned long long> temp_macro_times(other->n_valid_events);
        for (size_t i = 0; i < other->n_valid_events; i++) {
            temp_macro_times[i] = other->get_macro_time_at(i);
        }
        append_events(
                temp_macro_times.data(), static_cast<int>(other->n_valid_events),
                other->micro_times, static_cast<int>(other->n_valid_events),
                other->routing_channels, static_cast<int>(other->n_valid_events),
                other->event_types, static_cast<int>(other->n_valid_events),
                shift_macro_time,
                macro_time_offset
        );
    } else {
        append_events(
                other->macro_times, static_cast<int>(other->n_valid_events),
                other->micro_times, static_cast<int>(other->n_valid_events),
                other->routing_channels, static_cast<int>(other->n_valid_events),
                other->event_types, static_cast<int>(other->n_valid_events),
                shift_macro_time,
                macro_time_offset
        );
    }
}

void TTTR::append_event(
        unsigned long long macro_time,
        unsigned short micro_time,
        signed char routing_channel,
        signed char event_type,
        bool shift_macro_time,
        long long macro_time_offset
){
    append_events(
            &macro_time, 1,
            &micro_time, 1,
            &routing_channel, 1,
            &event_type, 1,
            shift_macro_time, macro_time_offset
    );
}


double TTTR::compute_count_rate(
        TTTR *tttr_data,
        std::vector<int> *tttr_indices,
        double macrotime_resolution
){
    double t_min = 1e60;
    double t_max = 0.0;
    std::vector<int> v;
    if(tttr_indices == nullptr){
        v.resize(tttr_data->n_valid_events);
        for(int i = 0; i < tttr_data->n_valid_events; i++) v[i] = i;
    } else{
        v = *tttr_indices;
    }
    for(auto &i: v){
        t_min = std::min((double) tttr_data->get_macro_time_at(i), t_min);
        t_max = std::max((double) tttr_data->get_macro_time_at(i), t_max);
    }
    if(macrotime_resolution < 0) {
        macrotime_resolution = tttr_data->header->get_macro_time_resolution();
    }
    auto n = (double) v.size();
    double dT = (t_max - t_min);
if (is_verbose()) {
    std::clog << "COMPUTE_COUNT_RATE" << std::endl;
    std::clog << "-- dT [mT units]:" << dT << std::endl;
    std::clog << "-- number of photons:" << n << std::endl;
    std::clog << "-- macrotime_resolution:" << macrotime_resolution << std::endl;
}
    return n  / (dT * macrotime_resolution);
}


double TTTR::compute_mean_microtime(
        TTTR *tttr_data,
        std::vector<int> *tttr_indices,
        double microtime_resolution,
        int minimum_number_of_photons
){
    if(microtime_resolution < 0)
        microtime_resolution = tttr_data->header->get_micro_time_resolution();
    double value = 0.0;
    double n = 0.0;
    if(tttr_indices == nullptr){
        // calculate mean arrival time iteratively
        for(int i = 0; i < tttr_data->n_valid_events; i++){
            value += 1. / (n + 1.) * (double) (tttr_data->micro_times[i] - value);
            n += 1.0;
        }
    } else{
        // calculate mean arrival time iteratively
        for(auto i: *tttr_indices){
            value += 1. / (n + 1.) * (double) (tttr_data->micro_times[i] - value);
            n += 1.0;
        }
    }
    value *= microtime_resolution;
    if (n < minimum_number_of_photons){
        value = -1.0;
    }
    return value;
}

void TTTR::compress_macro_times() {
    if (n_valid_events == 0) {
        return;
    }

    if (is_verbose()) {
        std::clog << "-- Compressing macro times for " << n_valid_events << " events using keyframes." << std::endl;
        std::clog << "-- Keyframe interval: " << keyframe_interval << " events" << std::endl;
    }

    // Calculate number of keyframes needed
    n_keyframes = (n_valid_events + keyframe_interval - 1) / keyframe_interval;
    
    if (is_verbose()) {
        std::clog << "-- Number of keyframes: " << n_keyframes << std::endl;
    }

    // Allocate keyframe storage
    if (macro_time_keyframes != nullptr) {
        free(macro_time_keyframes);
        
    }

    macro_time_keyframes = (unsigned long long*) malloc(n_keyframes * sizeof(unsigned long long));
    

    // Store keyframes (every keyframe_interval-th event)
    for (size_t i = 0; i < n_keyframes; i++) {
        size_t event_idx = i * keyframe_interval;
        if (event_idx < n_valid_events) {
            macro_time_keyframes[i] = macro_times[event_idx];
        } else {
            // Last keyframe for partial segment
            macro_time_keyframes[i] = macro_times[n_valid_events - 1];
        }
    }

    // The deltas are a column, added to the store rather than malloc'd. The
    // uncompressed macro times are dropped from the store at the end, which is
    // what actually returns the memory -- freeing the pointer would be freeing
    // a std::vector's buffer and aborts.
    if (col_macro_delta_ < 0) {
        col_macro_delta_ = events_.add_column("macro_time_delta",
                                              tttrlib::data::ColumnType::UInt32);
    }
    events_.column(col_macro_delta_).resize_uninitialized(capacity);
    sync_event_pointers();
    

    // Compress: store deltas relative to keyframes
    size_t warnings = 0;
    for (size_t i = 0; i < n_valid_events; i++) {
        size_t keyframe_idx = i / keyframe_interval;
        unsigned long long keyframe = macro_time_keyframes[keyframe_idx];
        
        if (macro_times[i] >= keyframe) {
            unsigned long long delta = macro_times[i] - keyframe;
            if (delta > UINT32_MAX) {
                if (warnings < 10) {  // Limit warnings
                    std::cerr << "WARNING: Delta from keyframe exceeds 32-bit range at index " << i 
                              << ". Delta: " << delta << std::endl;
                }
                warnings++;
                macro_times_compressed[i] = UINT32_MAX;
            } else {
                macro_times_compressed[i] = (uint32_t)delta;
            }
        } else {
            // This shouldn't happen with monotonic data
            if (warnings < 10) {
                std::cerr << "WARNING: Macro time before keyframe at index " << i << std::endl;
            }
            warnings++;
            macro_times_compressed[i] = 0;
        }
    }
    
    if (warnings > 10) {
        std::cerr << "... and " << (warnings - 10) << " more warnings suppressed." << std::endl;
        std::cerr << "Consider using a smaller keyframe interval." << std::endl;
    }

    // Drop the uncompressed column. Removing it from the store is what frees
    // the 64-bit macro times; there is nothing here to free by hand.
    if (col_macro_time_ >= 0) {
        const int removed = col_macro_time_;
        events_.remove_column(removed);
        col_macro_time_ = -1;
        // Removing a column shifts down only the indices AFTER it.
        auto after = [&](int& c) { if (c > removed) c -= 1; };
        after(col_macro_delta_); after(col_micro_time_);
        after(col_routing_channel_); after(col_event_type_);
        sync_event_pointers();
    }
    

    macro_time_compression_enabled = true;

    if (is_verbose()) {
        size_t saved_bytes = capacity * (sizeof(unsigned long long) - sizeof(uint32_t));
        std::clog << "-- Compression complete. Saved " << (saved_bytes / 1024.0 / 1024.0) 
                  << " MB (" << (saved_bytes * 100.0 / (capacity * sizeof(unsigned long long))) 
                  << "% reduction)." << std::endl;
    }
}

void TTTR::decompress_macro_times() {
    if (!macro_time_compression_enabled || macro_times_compressed == nullptr) {
        return;
    }

    if (is_verbose()) {
        std::clog << "-- Decompressing macro times for " << n_valid_events << " events." << std::endl;
    }

    // The 64-bit macro times come back as a column. The delta column is
    // removed at the end, which is what frees them -- the deltas belong to the
    // store, and calling free on that pointer aborts.
    if (col_macro_time_ < 0) {
        col_macro_time_ = events_.add_column("macro_time",
                                             tttrlib::data::ColumnType::UInt64);
    }
    events_.column(col_macro_time_).resize_uninitialized(capacity);
    sync_event_pointers();

    // Decompress: reconstruct absolute times from keyframes + deltas
    for (size_t i = 0; i < n_valid_events; i++) {
        size_t keyframe_idx = i / keyframe_interval;
        unsigned long long keyframe = macro_time_keyframes[keyframe_idx];
        macro_times[i] = keyframe + (unsigned long long)macro_times_compressed[i];
    }

    if (col_macro_delta_ >= 0) {
        events_.remove_column(col_macro_delta_);
        const int removed = col_macro_delta_;
        col_macro_delta_ = -1;
        auto after = [&](int& c) { if (c > removed) c -= 1; };
        after(col_macro_time_); after(col_micro_time_);
        after(col_routing_channel_); after(col_event_type_);
        sync_event_pointers();
    }
    free(macro_time_keyframes);
    macro_time_keyframes = nullptr;
    n_keyframes = 0;

    macro_time_compression_enabled = false;

    if (is_verbose()) {
        std::clog << "-- Decompression complete." << std::endl;
    }
}

bool TTTR::enable_macro_time_compression(size_t kf_interval, bool force) {
    if (macro_time_compression_enabled && !force) {
        if (is_verbose()) {
            std::clog << "-- Macro time compression already enabled." << std::endl;
        }
        return true;
    }

    if (macro_time_compression_enabled && force) {
        // Decompress first, then recompress
        decompress_macro_times();
    }

    // Set keyframe interval
    keyframe_interval = kf_interval;
    if (keyframe_interval == 0) {
        keyframe_interval = 1000000;  // Default
    }

    compress_macro_times();
    return macro_time_compression_enabled;  // Returns true if compression succeeded
}

void TTTR::disable_macro_time_compression() {
    if (!macro_time_compression_enabled) {
        if (is_verbose()) {
            std::clog << "-- Macro time compression already disabled." << std::endl;
        }
        return;
    }

    decompress_macro_times();
}

int TTTR::apply_luts_and_shifts(int seed, bool use_dithering) {
    bool verbose = is_verbose();
    
    if (verbose) {
        std::cout << "Applying LUTs and shifts with seed=" << seed << ", use_dithering=" << use_dithering << std::endl;
    }
    if (micro_times == nullptr || routing_channels == nullptr || n_valid_events == 0) {
        if (verbose) {
            std::cerr << "No data to process or NULL pointers" << std::endl;
        }
        return 0;
    }
    
    // Apply LUTs and shifts using the class MicrotimeLinearization in one pass
    if (mt_linearizer != nullptr && mt_linearizer->has_luts()) {
        if (verbose) {
            std::cout << "Using MicrotimeLinearization for LUT application" << std::endl;
        }
        
        // Determine seed to use
        unsigned int actual_seed = 0;
        if (seed >= 0) {
            actual_seed = static_cast<unsigned int>(seed);
        } else {
            // seed == -1, use environment variable
            const char* seed_env = std::getenv("TTTR_RND_SEED");
            if (seed_env) {
                try {
                    actual_seed = static_cast<unsigned int>(std::stoul(seed_env));
                } catch (const std::exception&) {
                    // Invalid seed value, use default
                    actual_seed = 0;
                }
            }
        }
        
        if (verbose) {
            std::cout << "Applying linearization to " << n_valid_events << " events" << std::endl;
        }
        
        // Apply linearization and shifts in one pass
        int result = mt_linearizer->linearize(
            micro_times,
            (const unsigned char*)routing_channels,
            (int)n_valid_events,
            actual_seed,
            use_dithering
        );
        
        // Update microtime resolution if LUTs were applied
        if (result != 0) {
            update_microtime_resolution_after_lut();
        }
        
        if (verbose) {
            if (result == 0) {
                std::clog << "WARNING: LUT/shift application failed" << std::endl;
            } else {
                std::clog << "LUTs and shifts applied successfully" << std::endl;
            }
        }
        
        return result;  // Return 1 on success, 0 on failure
    } else {
        if (verbose) {
            std::cout << "Using manual shift application (no LUTs configured)" << std::endl;
        }
        
        // No LUTs configured, just apply shifts manually
        int modified_events = 0;
        unsigned short n_micro_channels = get_number_of_micro_time_channels();
        const size_t LOOKUP_SIZE = TTTRLIB_MAX_ROUTING_CHANNELS;
        std::vector<int> channel_shift_lookup(LOOKUP_SIZE, 0);

        // Populate shift lookups from MicrotimeLinearization
        if (mt_linearizer != nullptr) {
            for (size_t i = 0; i < LOOKUP_SIZE; ++i) {
                channel_shift_lookup[i] = mt_linearizer->get_channel_shift(static_cast<int>(i));
            }
        }

        for (size_t i = 0; i < n_valid_events; ++i) {
            signed char channel = routing_channels[i];
            unsigned short microtime = micro_times[i];
            unsigned char channel_idx = static_cast<unsigned char>(channel);

            // Apply shift
            int shift = channel_shift_lookup[channel_idx];
            if (shift != 0) {
                unsigned short old_microtime = microtime;
                // Apply shift with wraparound only when microtime channels are properly defined (> 1)
                if (n_micro_channels > 1) {
                    microtime = (microtime + shift) % n_micro_channels;
                } else {
                    // No defined microtime channels, apply shift without wrapping
                    int new_value = static_cast<int>(microtime) + shift;
                    if (new_value < 0) {
                        new_value = 0;  // Clamp to 0
                    }
                    microtime = static_cast<unsigned short>(new_value);
                }
                if (old_microtime != microtime) {
                    modified_events++;
                }
                micro_times[i] = microtime;
            }
        }

        if (verbose) {
            std::cout << "Applied shifts to " << modified_events << " out of " << n_valid_events << " events" << std::endl;
        }

        return modified_events;
    }
}

int TTTR::apply_channel_luts(
    const std::map<int, std::vector<float>>& channel_luts,
    const std::map<signed char, int>& channel_shifts
) {
    if (is_verbose()) {
        std::clog << "Configuring channel LUTs and shifts..." << std::endl;
    }

    // Configure mt_linearizer with LUTs and shifts per channel
    if (mt_linearizer != nullptr) {
        // Set LUTs for each channel
        for (const auto& lut_entry : channel_luts) {
            int channel = lut_entry.first;
            mt_linearizer->set_channel_lut(channel, lut_entry.second);
        }
        
        // Set shifts for each channel
        for (const auto& shift_entry : channel_shifts) {
            signed char ch = shift_entry.first;
            int channel = static_cast<unsigned char>(ch);  // Convert to 0-255 range
            mt_linearizer->set_channel_shift(channel, shift_entry.second);
        }
    }

    if (is_verbose()) {
        std::clog << "Channel LUTs and shifts configured successfully." << std::endl;
    }

    return 1; // Success
}

MicrotimeLinearization* TTTR::get_mt_linearizer() {
    return mt_linearizer;
}

void TTTR::set_mt_linearizer(MicrotimeLinearization* mt_linearizer) {
    if (this->mt_linearizer != nullptr) {
        delete this->mt_linearizer;
    }
    this->mt_linearizer = mt_linearizer;
}

void TTTR::set_channel_luts(const float* luts, int n_channels, int lut_size) {
    if (mt_linearizer != nullptr) {
        mt_linearizer->set_channel_luts_from_array(luts, n_channels, lut_size);
    }
}

void TTTR::set_channel_shifts(const int* shifts, int n_channels) {
    if (mt_linearizer != nullptr) {
        mt_linearizer->set_channel_shifts_from_array(shifts, n_channels);
    }
}

void TTTR::get_channel_luts(float** luts, int* n_channels, int* lut_size) {
    if (mt_linearizer != nullptr) {
        mt_linearizer->get_channel_luts_as_array(luts, n_channels, lut_size);
    } else {
        *luts = nullptr;
        *n_channels = 0;
        *lut_size = 0;
    }
}

void TTTR::get_channel_shifts(int** shifts, int* n_channels) {
    if (mt_linearizer != nullptr) {
        mt_linearizer->get_channel_shifts_as_array(shifts, n_channels);
    } else {
        *shifts = nullptr;
        *n_channels = 0;
    }
}

void TTTR::update_microtime_resolution_after_lut() {
    if (mt_linearizer == nullptr || !mt_linearizer->has_luts()) {
        return;  // No LUTs applied, no resolution change
    }

    // Find which channels are actually used in the data
    std::unordered_set<unsigned char> used_channels;
    for (size_t i = 0; i < n_valid_events; ++i) {
        used_channels.insert(static_cast<unsigned char>(routing_channels[i]));
    }

    // Calculate average range transformation factor
    double total_factor = 0.0;
    int channel_count = 0;
    std::vector<double> factors;

    for (unsigned char channel : used_channels) {
        const std::vector<float>& lut = mt_linearizer->get_channel_lut(channel);
        if (!lut.empty()) {
            // Calculate range transformation: original_range / new_range
            // Original range is the LUT size (number of input bins)
            size_t original_range = lut.size();
            
            // New range is the span of LUT output values
            float min_val = *std::min_element(lut.begin(), lut.end());
            float max_val = *std::max_element(lut.begin(), lut.end());
            float new_range = max_val - min_val;
            
            if (new_range > 0) {
                double factor = static_cast<double>(original_range) / new_range;
                factors.push_back(factor);
                total_factor += factor;
                channel_count++;
                
                if (is_verbose()) {
                    std::clog << "Channel " << static_cast<int>(channel) 
                              << " LUT range transformation factor: " << factor 
                              << " (original: " << original_range << ", new: " << new_range << ")" << std::endl;
                }
            }
        }
    }

    if (channel_count > 0) {
        double avg_factor = total_factor / channel_count;
        
        // Check for significant variation in transformation factors
        if (channel_count > 1) {
            double variance = 0.0;
            for (double factor : factors) {
                double diff = factor - avg_factor;
                variance += diff * diff;
            }
            variance /= channel_count;
            double std_dev = std::sqrt(variance);
            
            // Warn if standard deviation is more than 10% of the average factor
            if (std_dev > 0.1 * avg_factor) {
                std::clog << "WARNING: LUT transformation factors vary significantly across channels "
                          << "(std_dev: " << std_dev << ", avg_factor: " << avg_factor << "). "
                          << "Microtime resolution update uses average factor." << std::endl;
            }
        }
        
        double old_resolution = header->get_micro_time_resolution();
        double new_resolution = old_resolution * avg_factor;
        
        header->set_micro_time_resolution(new_resolution);
        
        if (is_verbose()) {
            std::clog << "Updated microtime resolution: " << old_resolution 
                      << " ns -> " << new_resolution << " ns (factor: " << avg_factor << ")" << std::endl;
        }
    }
}

void TTTR::merge(const TTTR& other, unsigned long long offset_macro_time, int channel_offset, int strategy) {
    if (is_verbose()) {
        std::clog << "Merging TTTR data: " << other.n_valid_events 
                  << " events into " << n_valid_events << " existing events"
                  << " (strategy: " << (strategy == 0 ? "stack" : "interleave") << ")" << std::endl;
    }
    
    // For now, disable compression during merge
    bool was_compressed = macro_time_compression_enabled && macro_times_compressed;
    if (was_compressed) {
        decompress_macro_times();
    }
    
    // Calculate new total size
    size_t new_total_events = n_valid_events + other.n_valid_events;
    
    // Allocate new arrays
    unsigned long long* new_macro_times = nullptr;
    unsigned short* new_micro_times = nullptr;
    signed char* new_routing_channels = nullptr;
    signed char* new_event_types = nullptr;
    
    // Allocate memory for new arrays using appropriate allocator
    new_macro_times = (unsigned long long*) malloc(new_total_events * sizeof(unsigned long long));
    new_micro_times = (unsigned short*) malloc(new_total_events * sizeof(unsigned short));
    new_routing_channels = (signed char*) malloc(new_total_events * sizeof(signed char));
    new_event_types = (signed char*) malloc(new_total_events * sizeof(signed char));
    
    
    if (!new_macro_times || !new_micro_times || !new_routing_channels || !new_event_types) {
        std::cerr << "Error: Failed to allocate memory for merge operation" << std::endl;
        if (new_macro_times) free(new_macro_times);
        if (new_micro_times) free(new_micro_times);
        if (new_routing_channels) free(new_routing_channels);
        if (new_event_types) free(new_event_types);
        return;
    }
    
    if (strategy == 0) {
        // Stack merge: append events from other to this
        size_t idx = 0;
        
        // Copy existing events
        for (size_t i = 0; i < n_valid_events; i++) {
            new_macro_times[i] = macro_times[i];
            new_micro_times[i] = micro_times[i];
            new_routing_channels[i] = routing_channels[i];
            new_event_types[i] = event_types[i];
            idx++;
        }
        
        // Copy events from other TTTR with offsets
        for (size_t i = 0; i < other.n_valid_events; i++) {
            new_macro_times[idx] = other.macro_times[i] + offset_macro_time;
            new_micro_times[idx] = other.micro_times[i];
            
            // Apply channel offset with bounds checking
            int32_t new_channel = static_cast<int32_t>(other.routing_channels[i]) + channel_offset;
            if (new_channel < -128) new_channel = -128;
            if (new_channel > 127) new_channel = 127;
            new_routing_channels[idx] = static_cast<signed char>(new_channel);
            
            new_event_types[idx] = other.event_types[i];
            idx++;
        }
        
    } else if (strategy == 1) {
        // Interleave merge: merge by time, maintaining chronological order
        std::vector<std::pair<uint64_t, std::tuple<size_t, bool>>> merge_indices;
        
        // Add indices from this TTTR
        for (size_t i = 0; i < n_valid_events; i++) {
            merge_indices.emplace_back(macro_times[i], std::make_tuple(i, false));
        }
        
        // Add indices from other TTTR with offset
        for (size_t i = 0; i < other.n_valid_events; i++) {
            uint64_t offset_time = other.macro_times[i] + offset_macro_time;
            merge_indices.emplace_back(offset_time, std::make_tuple(i, true));
        }
        
        // Sort by macro time stably to preserve relative order of simultaneous events
        std::stable_sort(merge_indices.begin(), merge_indices.end(),
                 [](const auto& a, const auto& b) { return a.first < b.first; });
        
        // Copy merged data
        for (size_t i = 0; i < merge_indices.size(); i++) {
            const auto& item = merge_indices[i];
            const auto& index_info = item.second;
            size_t original_idx = std::get<0>(index_info);
            bool from_other = std::get<1>(index_info);
            
            if (from_other) {
                // Copy from other TTTR
                new_macro_times[i] = other.macro_times[original_idx] + offset_macro_time;
                new_micro_times[i] = other.micro_times[original_idx];
                
                // Apply channel offset with bounds checking
                int32_t new_channel = static_cast<int32_t>(other.routing_channels[original_idx]) + channel_offset;
                if (new_channel < -128) new_channel = -128;
                if (new_channel > 127) new_channel = 127;
                new_routing_channels[i] = static_cast<signed char>(new_channel);
                
                new_event_types[i] = other.event_types[original_idx];
            } else {
                // Copy from this TTTR
                new_macro_times[i] = macro_times[original_idx];
                new_micro_times[i] = micro_times[original_idx];
                new_routing_channels[i] = routing_channels[original_idx];
                new_event_types[i] = event_types[original_idx];
            }
        }
    }
    
    // Free old arrays using the proper deallocator
    deallocate_memory_of_records();
    
    // Update pointers
    macro_times = new_macro_times;
    micro_times = new_micro_times;
    routing_channels = new_routing_channels;
    event_types = new_event_types;
    
    // Update counts
    n_valid_events = new_total_events;
    n_records_in_file = new_total_events;
    capacity = new_total_events;
    
    if (is_verbose()) {
        std::clog << "Merge completed: " << n_valid_events << " total events" << std::endl;
    }
}
