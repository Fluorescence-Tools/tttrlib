#include "TTTR.h"
#include "Verbose.h"

// Static member definition outside the class
tttrlib::bimap<std::string, int> TTTR::container_names = TTTR::initialize_container_names();
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
    fp_records_begin = p2.fp_records_begin;

    used_routing_channels = p2.used_routing_channels;
    n_records_in_file = p2.n_records_in_file;
    n_records_read = p2.n_records_read;
    n_valid_events = p2.n_valid_events;
    fp_records_begin = p2.fp_records_begin;
    
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

TTTR::TTTR(const char *fn, int container_type, bool read_input) : TTTR(){
    if(container_type >= 0){
        tttr_container_type_str = container_names.right.at(container_type);
        tttr_container_type = container_type;
        filename.assign(fn);
        if(read_input){
            if(read_file())
                find_used_routing_channels();
        }
    } else{
        std::cerr << "File " << fn << " not supported." << std::endl;
    }
}

TTTR::TTTR(const char *fn, const char *container_type) : TTTR() {
    try {
        std::string container_type_str_lower(container_type);
        std::transform(container_type_str_lower.begin(), container_type_str_lower.end(),
                       container_type_str_lower.begin(), ::tolower);

        if (container_type_str_lower == "auto") {
            tttr_container_type = inferTTTRFileType(fn);
            tttr_container_type_str = container_names.right.at(tttr_container_type);
        } else {
            tttr_container_type_str.assign(container_type);
            tttr_container_type = container_names.left.at(std::string(container_type));
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

TTTR::TTTR(const char *fn, int container_type) : TTTR(fn, container_type, true) {
    try {
        tttr_container_type_str.assign(
                container_names.right.at(container_type)
        );
    }
    catch(...) {
        std::cerr << "TTTR::TTTR(const char *fn, int container_type): Container type " << container_type << " not supported." << std::endl;
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
    for (size_t i = 0; i < n_valid_events; i++) {
        signed char channel = routing_channels[i];
        if (std::find(used_routing_channels.begin(), used_routing_channels.end(), channel) == used_routing_channels.end()) {
            used_routing_channels.push_back(channel);
        }
    }
}

int TTTR::read_hdf_file(const char *fn) {
#ifdef BUILD_PHOTON_HDF
    header = new TTTRHeader();
    header->read_photon_hdf5_setup(fn);

    /* handles */
    hid_t ds_microtime = -1, ds_n_sync_pulses = -1, ds_routing_channels = -1;
    hid_t space = -1;

    /* dataset and chunk dimensions */
    hsize_t dims[1] = {0};

    /* open file */
    hdf5_file = H5Fopen(fn, H5F_ACC_RDONLY, H5P_DEFAULT);
    if (hdf5_file < 0) {
        std::cerr << "Error: Unable to open file: " << fn << std::endl;
        return 1;
    }

    /* Check and handle /photon_data/timestamps */
    if (H5Lexists(hdf5_file, "/photon_data/timestamps", H5P_DEFAULT) > 0) {
        ds_n_sync_pulses = H5Dopen(hdf5_file, "/photon_data/timestamps", H5P_DEFAULT);
        space = H5Dget_space(ds_n_sync_pulses);

        // Allocate memory
        H5Sget_simple_extent_dims(space, dims, nullptr);
        n_valid_events = dims[0];
        n_records_in_file = dims[0];
        allocate_memory_for_records(n_valid_events);
        
        // Read into temporary buffer if compression is enabled
        if (macro_time_compression_enabled) {
            unsigned long long* temp_macro_times = (unsigned long long*) malloc(n_valid_events * sizeof(unsigned long long));
            H5Dread(
                    ds_n_sync_pulses,
                    H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                    temp_macro_times
            );
            // Copy to compressed storage using set_macro_time_at
            for (size_t i = 0; i < n_valid_events; i++) {
                set_macro_time_at(i, temp_macro_times[i]);
            }
            free(temp_macro_times);
        } else {
            H5Dread(
                    ds_n_sync_pulses,
                    H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                    macro_times
            );
        }
        H5Sclose(space);
        H5Dclose(ds_n_sync_pulses);
    } else {
        std::cerr << "Warning: /photon_data/timestamps not found. Filling macro_times with zeros." << std::endl;
        if (macro_time_compression_enabled) {
            for (size_t i = 0; i < n_records_in_file; i++) {
                set_macro_time_at(i, 0);
            }
        } else {
            std::fill(macro_times, macro_times + n_records_in_file, 0);
        }
        return 1;
    }

    /* Check and handle /photon_data/detectors */
    if (H5Lexists(hdf5_file, "/photon_data/detectors", H5P_DEFAULT) > 0) {
        ds_routing_channels = H5Dopen(hdf5_file, "/photon_data/detectors", H5P_DEFAULT);
        space = H5Dget_space(ds_routing_channels);
        H5Sget_simple_extent_dims(space, dims, nullptr);
        H5Dread(
                ds_routing_channels,
                H5T_NATIVE_INT8, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                routing_channels
        );
        H5Sclose(space);
        H5Dclose(ds_routing_channels);
    } else {
        std::cerr << "Warning: /photon_data/detectors not found. Filling routing_channels with zeros." << std::endl;
        std::fill(routing_channels, routing_channels + n_records_in_file, 0);
    }

    /* Check and handle /photon_data/nanotimes */
    if (H5Lexists(hdf5_file, "/photon_data/nanotimes", H5P_DEFAULT) > 0) {
        ds_microtime = H5Dopen(hdf5_file, "/photon_data/nanotimes", H5P_DEFAULT);
        space = H5Dget_space(ds_microtime);
        H5Sget_simple_extent_dims(space, dims, nullptr);
        // Memory already allocated at line 256, don't reallocate
        H5Dread(
                ds_microtime,
                H5T_NATIVE_UINT16, H5S_ALL, H5S_ALL, H5P_DEFAULT,
                micro_times
        );
        H5Sclose(space);
        H5Dclose(ds_microtime);
    } else {
        std::cerr << "Warning: /photon_data/nanotimes not found. Filling micro_times with zeros." << std::endl;
        std::fill(micro_times, micro_times + n_records_in_file, 0);
    }

    /* Close the file */
    H5Fclose(hdf5_file);
    return 0;

#else
    std::cerr << "Not built with Photon HDF interface." << std::endl;
    return 1;
#endif
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
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, static_cast<long>(HEADER_SIZE), SEEK_SET); // Return to start of data after header

    if (fileSize < HEADER_SIZE + 26) {
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

void TTTR::alex_to_microtime(unsigned long alex_period, int period_shift) {
    for (size_t i = 0; i < n_valid_events; ++i) {
        int64_t m = get_macro_time_at(i) - period_shift;
        micro_times[i] = static_cast<unsigned short>(m % alex_period);
    }
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
    if (std::filesystem::exists(p)) {
if (is_verbose()) {
        std::clog << "-- Filename: " << std::filesystem::path(p).u8string() << std::endl;
}
        // store canonical UTF-8 string version (optional)
        this->filename = p.u8string();

        fn = this->filename.c_str();
        if (container_type == PHOTON_HDF_CONTAINER) {
            read_hdf_file(fn);
        } else if (container_type == SM_CONTAINER) {
            read_sm_file(fn);
        } else {
            fp = open_file(this->filename, "rb");
            header = new TTTRHeader(fp, container_type);
            fp_records_begin = header->end();
            tttr_record_type = header->get_tttr_record_type();
if (is_verbose()) {
            std::clog << "-- TTTR record type: " << tttr_record_type << std::endl;
}
            n_records_in_file = get_number_of_records_by_file_size(fp, header->header_end, header->get_bytes_per_record());
if (is_verbose()) {
            std::clog << "-- TTTR record type: " << tttr_record_type << std::endl;
            std::clog << "-- TTTR number of records: " << n_records_in_file << std::endl;
}
            allocate_memory_for_records(n_records_in_file);
            read_records();
            fclose(fp);
        }

        if (container_type == CZ_CONFOCOR3_CONTAINER) {
            // Confocor raw data has no channel number in events
            auto tag = header->get_tag(header->json_data, "channel");
            int channel = tag["value"];
if (is_verbose()) {
            std::clog << "-- Confocor3 channel: " << channel << std::endl;
}
            for(int i = 0; i < n_records_in_file; i++) {
                routing_channels[i] = channel;
            }
        }
if (is_verbose()) {
            std::clog << "-- Resulting number of TTTR entries: " << n_valid_events << std::endl;
            if (macro_time_compression_enabled) {
                std::clog << "-- Macro times compressed during read with " << n_keyframes << " keyframes" << std::endl;
            }
}
            return 1;
    } else {
        std::clog << "-- WARNING: File " << std::filesystem::path(p).u8string() << " does not exist" << std::endl;
        return 0;
    }
}


TTTR::~TTTR() {
    delete header;
    deallocate_memory_of_records();
}

std::string TTTR::get_filename() {
    return std::string(filename);
}

void TTTR::allocate_memory_for_records(size_t n_rec){
if (is_verbose()) {
    std::clog << "-- Allocating memory for " << n_rec << " TTTR records." << std::endl;
}
    
    // If auto-compression is enabled AND not disabled for this instance, allocate compressed storage
    if (auto_compress_on_read && macro_time_compression_enabled && n_rec > 0) {
if (is_verbose()) {
        std::clog << "-- Allocating compressed storage (32-bit deltas + keyframes)" << std::endl;
}
        // Calculate keyframes
        n_keyframes = (n_rec + keyframe_interval - 1) / keyframe_interval;
        
        if(tttr_container_type != PHOTON_HDF_CONTAINER) {
            macro_times_compressed = (uint32_t*) malloc(n_rec * sizeof(uint32_t));
            macro_time_keyframes = (unsigned long long*) malloc(n_keyframes * sizeof(unsigned long long));
            micro_times = (unsigned short*) malloc(n_rec * sizeof(unsigned short));
            routing_channels = (signed char*) malloc(n_rec * sizeof(signed char));
            event_types = (signed char*) malloc(n_rec * sizeof(signed char));
        } else {
            #ifdef BUILD_PHOTON_HDF
            macro_times_compressed = (uint32_t*) H5allocate_memory(n_rec * sizeof(uint32_t), false);
            macro_time_keyframes = (unsigned long long*) H5allocate_memory(n_keyframes * sizeof(unsigned long long), false);
            micro_times = (unsigned short*) H5allocate_memory(n_rec * sizeof(unsigned short), false);
            routing_channels = (signed char*) H5allocate_memory(n_rec * sizeof(signed char), false);
            event_types = (signed char*) H5allocate_memory(n_rec * sizeof(signed char), false);
            #endif
        }
        macro_times = nullptr;
        macro_time_compression_enabled = true;
        
if (is_verbose()) {
        size_t compressed_size = n_rec * sizeof(uint32_t) + n_keyframes * sizeof(unsigned long long);
        size_t uncompressed_size = n_rec * sizeof(unsigned long long);
        std::clog << "-- Compressed storage: " << (compressed_size / 1024.0 / 1024.0) << " MB" << std::endl;
        std::clog << "-- vs Uncompressed: " << (uncompressed_size / 1024.0 / 1024.0) << " MB" << std::endl;
        std::clog << "-- Saved: " << ((uncompressed_size - compressed_size) / 1024.0 / 1024.0) << " MB" << std::endl;
}
    } else {
        // Normal allocation (uncompressed)
        if(tttr_container_type != PHOTON_HDF_CONTAINER) {
            macro_times = (unsigned long long*) malloc(n_rec * sizeof(unsigned long long));
            micro_times = (unsigned short*) malloc(n_rec * sizeof(unsigned short));
            routing_channels = (signed char*) malloc(n_rec * sizeof(signed char));
            event_types = (signed char*) malloc(n_rec * sizeof(signed char));
        } else {
            #ifdef BUILD_PHOTON_HDF
            macro_times = (unsigned long long*) H5allocate_memory(n_rec * sizeof(unsigned long long), false);
            micro_times = (unsigned short*) H5allocate_memory(n_rec * sizeof(unsigned short), false);
            routing_channels = (signed char*) H5allocate_memory(n_rec * sizeof(signed char), false);
            event_types = (signed char*) H5allocate_memory(n_rec * sizeof(signed char), false);
            #endif
        }
        macro_times_compressed = nullptr;
        macro_time_keyframes = nullptr;
        macro_time_compression_enabled = false;
    }
    
    capacity = n_rec;
}

void TTTR::deallocate_memory_of_records(){

    if(tttr_container_type != PHOTON_HDF_CONTAINER) {
        free(macro_times);
        free(routing_channels);
        free(micro_times);
        free(event_types);
        if(macro_times_compressed != nullptr) {
            free(macro_times_compressed);
            macro_times_compressed = nullptr;
        }
        if(macro_time_keyframes != nullptr) {
            free(macro_time_keyframes);
            macro_time_keyframes = nullptr;
        }
    } else {
        #ifdef BUILD_PHOTON_HDF
        H5free_memory(macro_times);
        H5free_memory(routing_channels);
        H5free_memory(micro_times);
        H5free_memory(event_types);
        if(macro_times_compressed != nullptr) {
            H5free_memory(macro_times_compressed);
            macro_times_compressed = nullptr;
        }
        if(macro_time_keyframes != nullptr) {
            H5free_memory(macro_time_keyframes);
            macro_time_keyframes = nullptr;
        }
        H5garbage_collect();
        #endif
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
    
    if(tttr_container_type != PHOTON_HDF_CONTAINER) {
        macro_times = (unsigned long long*) realloc(
                macro_times, new_capacity * sizeof(unsigned long long)
        );
        micro_times = (unsigned short*) realloc(
                micro_times, new_capacity * sizeof(unsigned short)
        );
        routing_channels = (signed char*) realloc(
                routing_channels, new_capacity * sizeof(signed char)
        );
        event_types = (signed char*) realloc(
                event_types, new_capacity * sizeof(signed char)
        );
    } else {
        #ifdef BUILD_PHOTON_HDF
        // HDF5 memory cannot be reallocated, so we need to allocate new and copy
        unsigned long long* new_macro = (unsigned long long*) H5allocate_memory(
                new_capacity * sizeof(unsigned long long), false
        );
        unsigned short* new_micro = (unsigned short*) H5allocate_memory(
                new_capacity * sizeof(unsigned short), false
        );
        signed char* new_routing = (signed char*) H5allocate_memory(
                new_capacity * sizeof(signed char), false
        );
        signed char* new_event = (signed char*) H5allocate_memory(
                new_capacity * sizeof(signed char), false
        );
        
        // Copy old data
        if(capacity > 0) {
            memcpy(new_macro, macro_times, capacity * sizeof(unsigned long long));
            memcpy(new_micro, micro_times, capacity * sizeof(unsigned short));
            memcpy(new_routing, routing_channels, capacity * sizeof(signed char));
            memcpy(new_event, event_types, capacity * sizeof(signed char));
            
            // Free old memory
            H5free_memory(macro_times);
            H5free_memory(micro_times);
            H5free_memory(routing_channels);
            H5free_memory(event_types);
        }
        
        macro_times = new_macro;
        micro_times = new_micro;
        routing_channels = new_routing;
        event_types = new_event;
        #endif
    }
    
    capacity = new_capacity;
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
            switch(tttr_record_type) {
            case PQ_RECORD_TYPE_PHT3:
                process_records_batch<PQ_RECORD_TYPE_PHT3>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case PQ_RECORD_TYPE_PHT2:
                process_records_batch<PQ_RECORD_TYPE_PHT2>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case PQ_RECORD_TYPE_HHT3v1:
                process_records_batch<PQ_RECORD_TYPE_HHT3v1>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case PQ_RECORD_TYPE_HHT3v2:
                process_records_batch<PQ_RECORD_TYPE_HHT3v2>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case PQ_RECORD_TYPE_HHT2v1:
                process_records_batch<PQ_RECORD_TYPE_HHT2v1>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case PQ_RECORD_TYPE_HHT2v2:
                process_records_batch<PQ_RECORD_TYPE_HHT2v2>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case BH_RECORD_TYPE_SPC130:
                process_records_batch<BH_RECORD_TYPE_SPC130>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case BH_RECORD_TYPE_SPC600_256:
                process_records_batch<BH_RECORD_TYPE_SPC600_256>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case BH_RECORD_TYPE_SPC600_4096:
                process_records_batch<BH_RECORD_TYPE_SPC600_4096>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case CZ_RECORD_TYPE_CONFOCOR3:
                process_records_batch<CZ_RECORD_TYPE_CONFOCOR3>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    temp_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            default:
                // This should never happen - all record types have template specializations
                std::cerr << "ERROR: Unsupported TTTR record type: " << tttr_record_type << std::endl;
                std::cerr << "This is a bug - please report it!" << std::endl;
                free(tmp);
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
            switch(tttr_record_type) {
            case PQ_RECORD_TYPE_PHT3:
                process_records_batch<PQ_RECORD_TYPE_PHT3>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case PQ_RECORD_TYPE_PHT2:
                process_records_batch<PQ_RECORD_TYPE_PHT2>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case PQ_RECORD_TYPE_HHT3v1:
                process_records_batch<PQ_RECORD_TYPE_HHT3v1>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case PQ_RECORD_TYPE_HHT3v2:
                process_records_batch<PQ_RECORD_TYPE_HHT3v2>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case PQ_RECORD_TYPE_HHT2v1:
                process_records_batch<PQ_RECORD_TYPE_HHT2v1>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case PQ_RECORD_TYPE_HHT2v2:
                process_records_batch<PQ_RECORD_TYPE_HHT2v2>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case BH_RECORD_TYPE_SPC130:
                process_records_batch<BH_RECORD_TYPE_SPC130>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case BH_RECORD_TYPE_SPC600_256:
                process_records_batch<BH_RECORD_TYPE_SPC600_256>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case BH_RECORD_TYPE_SPC600_4096:
                process_records_batch<BH_RECORD_TYPE_SPC600_4096>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            case CZ_RECORD_TYPE_CONFOCOR3:
                process_records_batch<CZ_RECORD_TYPE_CONFOCOR3>(
                    tmp, number_of_objects, bytes_per_record, overflow_counter,
                    macro_ptr, micro_ptr, routing_ptr, event_ptr, n_valid_events);
                break;
            default:
                std::cerr << "ERROR: Unsupported TTTR record type: " << tttr_record_type << std::endl;
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
    *output = new unsigned long long[*n_output];

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
    
    if(tttr_container_type != PHOTON_HDF_CONTAINER) {
        macro_times = (unsigned long long*) realloc(
                macro_times, n_valid_events * sizeof(unsigned long long)
        );
        micro_times = (unsigned short*) realloc(
                micro_times, n_valid_events * sizeof(unsigned short)
        );
        routing_channels = (signed char*) realloc(
                routing_channels, n_valid_events * sizeof(signed char)
        );
        event_types = (signed char*) realloc(
                event_types, n_valid_events * sizeof(signed char)
        );
    } else {
        #ifdef BUILD_PHOTON_HDF
        // HDF5 memory cannot be reallocated, so we need to allocate new and copy
        unsigned long long* new_macro = (unsigned long long*) H5allocate_memory(
                n_valid_events * sizeof(unsigned long long), false
        );
        unsigned short* new_micro = (unsigned short*) H5allocate_memory(
                n_valid_events * sizeof(unsigned short), false
        );
        signed char* new_routing = (signed char*) H5allocate_memory(
                n_valid_events * sizeof(signed char), false
        );
        signed char* new_event = (signed char*) H5allocate_memory(
                n_valid_events * sizeof(signed char), false
        );
        
        // Copy data
        memcpy(new_macro, macro_times, n_valid_events * sizeof(unsigned long long));
        memcpy(new_micro, micro_times, n_valid_events * sizeof(unsigned short));
        memcpy(new_routing, routing_channels, n_valid_events * sizeof(signed char));
        memcpy(new_event, event_types, n_valid_events * sizeof(signed char));
        
        // Free old memory
        H5free_memory(macro_times);
        H5free_memory(micro_times);
        H5free_memory(routing_channels);
        H5free_memory(event_types);
        
        macro_times = new_macro;
        micro_times = new_micro;
        routing_channels = new_routing;
        event_types = new_event;
        #endif
    }
    
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
    if(macro_time_calibration < 0){
        macro_time_calibration = header->get_macro_time_resolution();
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
    // use the file size and the header to calculate the number of records
    // the position of the first record in the file
    auto current_position = (size_t) ftell(fp);
    fseek(fp, 0L, SEEK_END);
    auto fileSize = (size_t) ftell(fp);
    // calculate the number of records based on the size of the file
    // and the bytes per record
    n_records_in_file = (fileSize - offset) / bytes_per_record;
    // move back to the original position
    fseek(fp, (long) current_position, SEEK_SET);
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



std::vector<long long> TTTR::burst_search(int L, int m, double T)
{
    // If there aren't enough events for a window of size m, return empty.
    if (static_cast<int64_t>(size()) < m) {
        return {};
    }

    bool in_burst = false;
    int64_t i_start = 0, i_stop = 0;

    // Convert time T to “macro-time” units
    const long long Ti = static_cast<long long>(T / header->get_macro_time_resolution());

    std::vector<long long> bursts;
    bursts.reserve(1000);

    // Loop over all valid starting positions
    // i + m - 1 <= size() - 1  ==>  i <= size() - m
    int64_t n_events = static_cast<int64_t>(size());
    for (int64_t i = 0; i <= n_events - m; ++i) {

        // Check the time span of the window [i, i + m - 1]
        if (get_macro_time_at(i + m - 1) - get_macro_time_at(i) <= static_cast<unsigned long long>(Ti)) {
            // We have a valid burst window
            if (!in_burst) {
                in_burst = true;
                i_start = i;
            }
        }
        else {
            // The window fails the time condition
            if (in_burst) {
                // We just ended a burst
                in_burst = false;
                i_stop = i + m - 2; // last index in the valid window
                // Check if it meets the length requirement
                if (i_stop - i_start + 1 >= L) {
                    bursts.push_back(i_start);
                    bursts.push_back(i_stop);
                }
            }
        }
    }

    // If we exit the loop but are still in a burst, it extends to the last event
    if (in_burst) {
        i_stop = static_cast<int64_t>(size()) - 1;  // last valid index
        if (i_stop - i_start + 1 >= L) {
            bursts.push_back(i_start);
            bursts.push_back(i_stop);
        }
    }

    return bursts;
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


void TTTR::write_spc132_events(FILE* fp, TTTR* tttr){
    bh_overflow_t overflow;
    overflow.bits.empty = 0;
    overflow.bits.mtov = 1;
    overflow.bits.invalid = 1;

    bh_spc130_record_t record;
    unsigned dMT;
    unsigned long long MT_ov_last;
    unsigned long long MT_ov = 0;
    for (size_t n = 0; n < tttr->size(); n++) {
        // time since last macro_time record
        dMT = static_cast<unsigned>(tttr->get_macro_time_at(n) - MT_ov * 4096ULL);
        // Count the number of MT overflows
        MT_ov_last = dMT / 4096;
        // Subtract MT overflows from dMT
        dMT -= static_cast<unsigned>(MT_ov_last * 4096ULL);
        // increment the global overflow counter
        MT_ov += MT_ov_last;
        // write overflows
        while (MT_ov_last > 1) {
            // we fit 65536 = 2**16 in each overflow record
            overflow.bits.cnt = std::min(65536ull, MT_ov_last);
            fwrite(&overflow, 4, 1, fp);
            MT_ov_last -= overflow.bits.cnt;
        }
        {
            // we wrote all macro time overflows that do not fit
            // in the record. Thus, we have a valid photon
            record.bits.mt = dMT;
            record.bits.adc = 4095 - micro_times[n];
            record.bits.rout = routing_channels[n];
            // If there is a overflow set mtov
            record.bits.mtov = (MT_ov_last == 1);
            record.bits.invalid = 0;
            fwrite(&record, 4, 1, fp);
        }
    }
}

void TTTR::write_hht3v2_events(FILE* fp, TTTR* tttr){
    pq_hh_t3_record_t rec;

    unsigned dMT;
    unsigned int MT_ov_last;
    unsigned int MT_ov = 0;
    const unsigned int T3WRAPAROUND = 1024;

    for (size_t n = 0; n < tttr->size(); n++) {
        // time since last macro_time record
        dMT = static_cast<unsigned>(tttr->get_macro_time_at(n) - static_cast<unsigned long long>(MT_ov) * T3WRAPAROUND);
        // Count the number of MT overflows
        MT_ov_last = dMT / T3WRAPAROUND;
        // Subtract MT overflows from dMT
        dMT -= static_cast<unsigned>(static_cast<unsigned long long>(MT_ov_last) * T3WRAPAROUND);
        // increment the global overflow counter
        MT_ov += MT_ov_last;
        // write overflows
        while (MT_ov_last > 0) {
            rec.bits.special = 1;
            rec.bits.channel = 0x3F;
            rec.bits.n_sync = std::min(T3WRAPAROUND - 1, MT_ov_last);
            fwrite(&rec, 4, 1, fp);
            MT_ov_last -= rec.bits.n_sync;
        }
        {
            rec.bits.special = tttr->event_types[n];
            rec.bits.channel = tttr->routing_channels[n];
            rec.bits.n_sync = dMT;
            rec.bits.dtime = micro_times[n];
            fwrite(&rec, 4, 1, fp);
        }
    }
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
    } else if(container_type == PQ_PTU_CONTAINER){
        TTTRHeader::write_ptu_header(fn, header);
    } else if(container_type == PQ_HT3_CONTAINER){
        TTTRHeader::write_ht3_header(fn, header);
    }else{
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
    if(
            (container_type == PQ_PTU_CONTAINER) ||
            (container_type == PQ_HT3_CONTAINER)
            ){
        switch (record_type) {
            case PQ_RECORD_TYPE_HHT2v1:
            case PQ_RECORD_TYPE_HHT3v2:
            case PQ_RECORD_TYPE_HHT2v2:
            case PQ_RECORD_TYPE_HHT3v1:
            case PQ_RECORD_TYPE_PHT2:
            case PQ_RECORD_TYPE_PHT3:
                return true;
            default:
                return false;
        }
    } else if(container_type == BH_SPC130_CONTAINER){
        if(record_type == BH_RECORD_TYPE_SPC130){
            return true;
        } else{
            return false;
        }
    } else if(container_type == BH_SPC600_256_CONTAINER){
        if(record_type == BH_RECORD_TYPE_SPC600_256)
            return true;
        else
            return false;
    } else if(container_type == BH_SPC600_4096_CONTAINER){
        if(record_type == BH_RECORD_TYPE_SPC600_4096)
            return true;
        else
            return false;
    }
    return false;
}

bool TTTR::write(std::string filename, TTTRHeader* header){
    if(header == nullptr) header=this->header;
    int record_type =header->get_tttr_record_type();
    int container_type = header->get_tttr_container_type();
    if(!valid_container_record_pair(container_type, record_type)){
        std::cerr << "ERROR in TTTR::write: invalid container record combination." << std::endl;
        return false;
    }
    write_header(filename, header);
    fp = open_file(filename, "ab");
    if (fp != nullptr) {
        // append records
        if (record_type == BH_RECORD_TYPE_SPC130) {
            write_spc132_events(fp, this);
        } else if(record_type == PQ_RECORD_TYPE_HHT3v2){
            write_hht3v2_events(fp, this);
        } else{
            std::cerr << "ERROR: Record type " << record_type << " not supported" << std::endl;
        }
    } else {
        std::cerr << "ERROR: Cannot write to file: "
        << filename <<  std::endl;
        return false;
    }
    fclose(fp);
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

    // Default resolution and base channel count
    double resolution = 1.0;
    int base_channels = 0;

    auto header_ptr = tttr_data->get_header();
    if (header_ptr) {
        auto header = *header_ptr;
        double hdr_res = header.get_micro_time_resolution();
        resolution = (hdr_res > 0 ? hdr_res : 1.0);
        base_channels = header.get_number_of_micro_time_channels();
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

    // Collect micro-time values
    std::vector<unsigned short> selected;
    if (!tttr_indices) {
        unsigned short* micro_times = nullptr;
        int n_micro_times = 0;
        tttr_data->get_micro_times(&micro_times, &n_micro_times);
        if (micro_times && n_micro_times > 0) {
            selected.reserve(n_micro_times);
            for (int i = 0; i < n_micro_times; ++i) {
                if (!routing_channels ||
                    std::find(routing_channels->begin(), routing_channels->end(),
                              tttr_data->routing_channels[i]) != routing_channels->end()) {
                    selected.push_back(micro_times[i] / micro_time_coarsening);
                }
            }
        }
    } else {
        selected.reserve(tttr_indices->size());
        for (int idx : *tttr_indices) {
            if (!routing_channels ||
                std::find(routing_channels->begin(), routing_channels->end(),
                          tttr_data->routing_channels[idx]) != routing_channels->end()) {
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
        if (tttr_container_type != PHOTON_HDF_CONTAINER) {
            free(macro_time_keyframes);
        } else {
            #ifdef BUILD_PHOTON_HDF
            H5free_memory(macro_time_keyframes);
            #endif
        }
    }

    if (tttr_container_type != PHOTON_HDF_CONTAINER) {
        macro_time_keyframes = (unsigned long long*) malloc(n_keyframes * sizeof(unsigned long long));
    } else {
        #ifdef BUILD_PHOTON_HDF
        macro_time_keyframes = (unsigned long long*) H5allocate_memory(n_keyframes * sizeof(unsigned long long), false);
        #endif
    }

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

    // Allocate compressed storage
    if (macro_times_compressed != nullptr) {
        if (tttr_container_type != PHOTON_HDF_CONTAINER) {
            free(macro_times_compressed);
        } else {
            #ifdef BUILD_PHOTON_HDF
            H5free_memory(macro_times_compressed);
            #endif
        }
    }

    if (tttr_container_type != PHOTON_HDF_CONTAINER) {
        macro_times_compressed = (uint32_t*) malloc(capacity * sizeof(uint32_t));
    } else {
        #ifdef BUILD_PHOTON_HDF
        macro_times_compressed = (uint32_t*) H5allocate_memory(capacity * sizeof(uint32_t), false);
        #endif
    }

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

    // Free the original 64-bit storage
    if (tttr_container_type != PHOTON_HDF_CONTAINER) {
        free(macro_times);
        macro_times = nullptr;
    } else {
        #ifdef BUILD_PHOTON_HDF
        H5free_memory(macro_times);
        macro_times = nullptr;
        #endif
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

    // Allocate 64-bit storage
    if (tttr_container_type != PHOTON_HDF_CONTAINER) {
        macro_times = (unsigned long long*) malloc(capacity * sizeof(unsigned long long));
    } else {
        #ifdef BUILD_PHOTON_HDF
        macro_times = (unsigned long long*) H5allocate_memory(capacity * sizeof(unsigned long long), false);
        #endif
    }

    // Decompress: reconstruct absolute times from keyframes + deltas
    for (size_t i = 0; i < n_valid_events; i++) {
        size_t keyframe_idx = i / keyframe_interval;
        unsigned long long keyframe = macro_time_keyframes[keyframe_idx];
        macro_times[i] = keyframe + (unsigned long long)macro_times_compressed[i];
    }

    // Free compressed storage
    if (tttr_container_type != PHOTON_HDF_CONTAINER) {
        free(macro_times_compressed);
        free(macro_time_keyframes);
    } else {
        #ifdef BUILD_PHOTON_HDF
        H5free_memory(macro_times_compressed);
        H5free_memory(macro_time_keyframes);
        #endif
    }
    macro_times_compressed = nullptr;
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

int TTTR::linearize_microtimes(
    const MicrotimeLinearization& linearizer,
    unsigned int seed
) {
    if (micro_times == nullptr || routing_channels == nullptr) {
        return 0;
    }
    
    if (n_valid_events == 0) {
        return 1;
    }
    
    if (!linearizer.is_valid()) {
        if (is_verbose()) {
            std::clog << "-- ERROR: Invalid MicrotimeLinearization object" << std::endl;
        }
        return 0;
    }
    
    if (is_verbose()) {
        std::clog << "-- Linearizing " << n_valid_events << " microtimes with channel mapping..." << std::endl;
    }
    
    // Apply linearization using the MicrotimeLinearization object
    // which handles channel-to-card mapping internally
    int result = const_cast<MicrotimeLinearization&>(linearizer).linearize(
        micro_times,
        (const unsigned char*)routing_channels,
        (int)n_valid_events,
        seed
    );
    
    if (result && is_verbose()) {
        std::clog << "-- Microtime linearization complete." << std::endl;
    }
    
    return result;
}

int TTTR::linearize_microtimes_with_random(
    const MicrotimeLinearization& linearizer,
    const float* random_numbers,
    int n_random_numbers
) {
    if (micro_times == nullptr || routing_channels == nullptr) {
        return 0;
    }
    
    if (n_valid_events == 0) {
        return 1;
    }
    
    if (random_numbers == nullptr || n_random_numbers < (int)n_valid_events) {
        if (is_verbose()) {
            std::clog << "-- ERROR: Insufficient random numbers provided" << std::endl;
        }
        return 0;
    }
    
    if (!linearizer.is_valid()) {
        if (is_verbose()) {
            std::clog << "-- ERROR: Invalid MicrotimeLinearization object" << std::endl;
        }
        return 0;
    }
    
    if (is_verbose()) {
        std::clog << "-- Linearizing " << n_valid_events << " microtimes with provided random numbers..." << std::endl;
    }
    
    // Apply linearization with provided random numbers
    int result = const_cast<MicrotimeLinearization&>(linearizer).linearize(
        micro_times,
        (const unsigned char*)routing_channels,
        (int)n_valid_events,
        random_numbers
    );
    
    if (result && is_verbose()) {
        std::clog << "-- Microtime linearization complete." << std::endl;
    }
    
    return result;
}
