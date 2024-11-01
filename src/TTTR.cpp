#include "TTTR.h"

// Static member definition outside the class
boost::bimap<std::string, int> TTTR::container_names = TTTR::initialize_container_names();


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
        micro_times(nullptr),
        routing_channels(nullptr),
        event_types(nullptr),
        // protected
        n_records_in_file(0),
        n_records_read(0),
        n_valid_events(0),
        processRecord(nullptr){
    header = new TTTRHeader(tttr_container_type);
    allocate_memory_for_records(0);
}

TTTR::TTTR(unsigned long long *macro_times, int n_macrotimes,
           unsigned short *micro_times, int n_microtimes,
           signed char *routing_channels, int n_routing_channels,
           signed char *event_types, int n_event_types,
           bool find_used_channels
): TTTR() {
#ifdef VERBOSE_TTTRLIB
    std::clog << "INITIALIZING FROM VECTORS" << std::endl;
#endif
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
        this->macro_times[i] = macro_times[i];
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
#ifdef VERBOSE_TTTRLIB
    std::clog << "INITIALIZING FROM SELECTION" << std::endl;
#endif
    copy_from(parent, false);
    n_valid_events = (size_t) n_selection;
    if ((size_t) n_selection > parent.n_valid_events) {
        std::clog << "WARNING: The dimension of the selection exceeds the parents dimension." << std::endl;
    }
    allocate_memory_for_records(n_selection);
    for(size_t sel_i = 0; sel_i < n_selection; sel_i++){
        auto sel = selection[sel_i];
        sel = (sel < 0) ? parent.n_valid_events + sel : sel;
        macro_times[sel_i] = parent.macro_times[sel];
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
    if (include_big_data){
        allocate_memory_for_records(p2.n_valid_events);
        for (size_t i = 0; i < p2.n_valid_events; i++) {
            macro_times[i] = p2.macro_times[i];
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
        tttr_container_type_str.assign(container_type);
        tttr_container_type = container_names.left.at(std::string(container_type));
        filename.assign(fn);
        if(read_file())
            find_used_routing_channels();
    }
    catch(...) {
        std::cerr << "TTTR::TTTR(const char *fn, const char *container_type): Container type " << container_type << " not supported." << std::endl;
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
        macro_times[i] += shift;
    }
}

void TTTR::find_used_routing_channels(){
    used_routing_channels.clear();
    for(size_t i = 0; i < n_valid_events; i++){
        short c = routing_channels[i];
        bool new_channel = true;
        for(auto l: used_routing_channels){
            if(c == l){
                new_channel = false;
                break;
            }
        }
        if(new_channel){
            used_routing_channels.push_back(c);
        }
    }
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Used routing channels: ";
    for(auto c: used_routing_channels){
        std::clog << static_cast<unsigned>(c) << ", ";
    }
    std::clog << std::endl;
#endif
}

int TTTR::read_hdf_file(const char *fn){
#ifdef BUILD_PHOTON_HDF
    header = new TTTRHeader();

    /* handles */
    hid_t       ds_microtime, ds_n_sync_pulses, ds_routing_channels;
    hid_t       space;

    /* dataset and chunk dimensions*/
    hsize_t     dims[1];

    /* open file */
    hdf5_file = H5Fopen(fn, H5F_ACC_RDONLY, H5P_DEFAULT);
    ds_microtime        = H5Dopen(hdf5_file, "/photon_data/nanotimes", H5P_DEFAULT);
    ds_n_sync_pulses    = H5Dopen(hdf5_file, "/photon_data/timestamps", H5P_DEFAULT);
    ds_routing_channels = H5Dopen(hdf5_file, "/photon_data/detectors", H5P_DEFAULT);

    /*
     * Get dataspace and allocate memory for read buffer.
     */
    space = H5Dget_space(ds_microtime);
    H5Sget_simple_extent_dims(space, dims, nullptr);
    allocate_memory_for_records(dims[0]);
    /* All records are assumed to be valid.
     * Invalid records are usually sorted out
     * before storing the hdf5 file. */
    n_valid_events = dims[0];
    n_records_in_file = dims[0];
#ifdef VERBOSE_TTTRLIB
    std::clog << "n_records_in_file: " << n_records_in_file << std::endl;
#endif
    /*
     * Read the data
     */
    H5Dread(
            ds_microtime,
            H5T_NATIVE_UINT16, H5S_ALL, H5S_ALL, H5P_DEFAULT,
            micro_times
    );
    H5Dread(
            ds_n_sync_pulses,
            H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT,
            macro_times
    );
    H5Dread(
            ds_routing_channels,
            H5T_NATIVE_INT8, H5S_ALL, H5S_ALL, H5P_DEFAULT,
            routing_channels
    );
    /* close file */
    H5Fclose(hdf5_file);
    H5Sclose(space);
    return 1;
#endif
    std::cerr << "Not build with Photon HDF interface." << std::endl;
    return 0;
}

int TTTR::read_sm_file(const char *filename){
    // Function to read a 64-bit big-endian value

    // Open the file using fopen
    FILE* fp = fopen(filename, "rb");
    if (fp == nullptr) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return 1;
    }

    if (!fp) {
        std::cerr << "Error opening file: " << filename << std::endl;
        return 1;
    }

    // Decode header
    header = new TTTRHeader(fp, SM_CONTAINER);

    // Skip the header (165 bytes)
    size_t HEADER_SIZE = header->header_end;
    if (fseek(fp, HEADER_SIZE, SEEK_SET) != 0) {
        std::cerr << "Error seeking past the header." << std::endl;
        fclose(fp);
        return 1;
    }

    // Determine file size to calculate remaining data size
    fseek(fp, 0, SEEK_END);
    long fileSize = ftell(fp);
    fseek(fp, HEADER_SIZE, SEEK_SET); // Return to start of data after header

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

    auto readBigEndian16 = [](const uint8_t* data) -> uint16_t {
        return (static_cast<uint16_t>(data[0]) << 8) | static_cast<uint16_t>(data[1]);
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
        macro_times[i] = readBigEndian64(record);

        // Read and interpret the 16-bit detector
        routing_channels[i] = readBigEndian16(record + 8);

        // Set micro time to default value
        micro_times[i] = 0;

    }

    return 0;

}

int TTTR::read_file(const char *fn, int container_type) {
#ifdef VERBOSE_TTTRLIB
    std::clog << "READING TTTR FILE" << std::endl;
#endif
    if(fn == nullptr){
        fn = filename.c_str();
    }
    if(container_type < 0){
        container_type = tttr_container_type;
    }

    // check if file exists
    std::ifstream f(fn);
    if(f.good()){
#ifdef VERBOSE_TTTRLIB
        std::clog << "-- Filename: " << fn << std::endl;
        std::clog << "-- Container type: " << container_type << std::endl;
#endif
        fn = filename.c_str();
        if (container_type == PHOTON_HDF_CONTAINER) {
            read_hdf_file(fn);
        } else if (container_type == SM_CONTAINER){
            read_sm_file(fn);
        }
        else{
            fp = fopen(fn, "rb");
            header = new TTTRHeader(fp, container_type);
            fp_records_begin = header->end();
            tttr_record_type = header->get_tttr_record_type();
#ifdef VERBOSE_TTTRLIB
            std::clog << "-- TTTR record type: " << tttr_record_type << std::endl;
#endif
            processRecord = processRecord_map[tttr_record_type];
            n_records_in_file = get_number_of_records_by_file_size(fp, header->header_end, header->get_bytes_per_record());
#ifdef VERBOSE_TTTRLIB
            std::clog << "-- TTTR record type: " << tttr_record_type << std::endl;
            std::clog << "-- TTTR number of records: " << n_records_in_file << std::endl;
#endif
            allocate_memory_for_records(n_records_in_file);
            read_records();
            fclose(fp);
        } if( container_type == CZ_CONFOCOR3_CONTAINER) {
            // Confocor raw data has no channel number in events
            auto tag = header->get_tag(header->json_data, "channel");
            int channel = tag["value"];
#ifdef VERBOSE_TTTRLIB
            std::clog << "-- Confocor3 channel: " << channel << std::endl;
#endif
            for(int i = 0; i < n_records_in_file; i++) {
                routing_channels[i] = channel;
            }
        }
#ifdef VERBOSE_TTTRLIB
            std::clog << "-- Resulting number of TTTR entries: " << n_valid_events << std::endl;
#endif
            return 1;
    } else{
        std::clog << "-- WARNING: File " << filename << " does not exist" << std::endl;
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
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Allocating memory for " << n_rec << " TTTR records." << std::endl;
#endif
    if(tttr_container_type != PHOTON_HDF_CONTAINER) {
        macro_times = (unsigned long long*) malloc(
                n_rec * sizeof(unsigned long long)
                );
        micro_times = (unsigned short*) malloc(
                n_rec * sizeof(unsigned int)
        );
        routing_channels = (signed char*) malloc(
                n_rec * sizeof(signed char)
        );
        event_types = (signed char*) malloc(
                n_rec * sizeof(signed char)
        );
    } 
    else {
        #ifdef BUILD_PHOTON_HDF
        macro_times = (unsigned long long*) H5allocate_memory(
                n_rec * sizeof(unsigned long long), false
        );
        micro_times = (unsigned short*) H5allocate_memory(
                n_rec * sizeof(unsigned short), false
        );
        routing_channels = (signed char*) H5allocate_memory(
                n_rec * sizeof(signed char), false
        );
        event_types = (signed char*) H5allocate_memory(
                n_rec * sizeof(signed char), false
        );
        #endif
    }
}

void TTTR::deallocate_memory_of_records(){

    if(tttr_container_type != PHOTON_HDF_CONTAINER) {
        free(macro_times);
        free(routing_channels);
        free(micro_times);
        free(event_types);
    } else {
        #ifdef BUILD_PHOTON_HDF
        H5free_memory(macro_times);
        H5free_memory(routing_channels);
        H5free_memory(micro_times);
        H5free_memory(event_types);
        H5garbage_collect();
        #endif
    }
}

void TTTR::read_records(
        size_t n_rec,
        bool rewind,
        size_t chunk
) {
    n_rec = n_rec < n_records_in_file ? n_rec : n_records_in_file;
#ifdef VERBOSE_TTTRLIB
    std::cout << "-- Records that will be read : " << n_rec << std::endl;
#endif
    if(rewind) fseek(fp, (long) fp_records_begin, SEEK_SET);

    // The data is read in two steps. In the first step bigger data chunks
    // are read. In the second the records are read and processed record by
    // record.
    n_records_read = 0;
    overflow_counter = 0;
    n_valid_events = 0;
    size_t offset;

    // read data in chunks to speed up the access
    size_t number_of_objects;
    size_t bytes_per_record = header->get_bytes_per_record();
#ifdef VERBOSE_TTTRLIB
    std::cout << "-- Records that will be read : " << n_rec << std::endl;
    std::cout << "-- Bytes per record : " << bytes_per_record << std::endl;
#endif
    do{
        // Adjust chunk size if it's bigger than remaining records
        size_t remaining_records = n_rec - n_records_read;
        size_t adjusted_chunk = remaining_records < chunk ? remaining_records : chunk;

        auto tmp = (signed char *)malloc(bytes_per_record * adjusted_chunk);
        if (!tmp) {
            // Handle memory allocation failure
            std::cerr << "Memory allocation failed!" << std::endl;
            return;
        }
        number_of_objects = fread(tmp, bytes_per_record, adjusted_chunk, fp);

        for (size_t j = 0; j < number_of_objects; j++) {
            offset = bytes_per_record * j;
            n_valid_events += processRecord(
                    *(uint32_t *) &tmp[offset],
                    overflow_counter,
                    *(uint64_t *) &macro_times[n_valid_events],
                    *(uint32_t *) &micro_times[n_valid_events],
                    *(int16_t *)  &routing_channels[n_valid_events],
                    *(int16_t *)  &event_types[n_valid_events]
            );
        }
        free(tmp);
        n_records_read += number_of_objects;
    } while(number_of_objects > 0);
}

void TTTR::read_records(size_t n_rec){
    read_records(n_rec, true, 16384);
}

void TTTR::read_records() {
    read_records(n_records_in_file);
}

TTTRHeader* TTTR::get_header() {
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- TTTR::get_header" << std::endl;
#endif
    if(header != nullptr){
        return header;
    } else{
        std::clog << "WARNING: TTTR::header not initialized. Returning empty Header." << std::endl;
        header = new TTTRHeader();
        return header;
    }
}

void TTTR::set_header(TTTRHeader* v) {
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- TTTR::set_header" << std::endl;
#endif
    if(v != nullptr){
        header = new TTTRHeader(*v);
    }
}

void TTTR::get_macro_times(unsigned long long** output, int* n_output){
    get_array<unsigned long long>(n_valid_events, macro_times, output, n_output);
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
    selection_by_count_rate(
            output, n_output,
            macro_times, (int) n_valid_events,
            time_window, n_ph_max,
            header->get_macro_time_resolution(),
            invert, make_mask
    );
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
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Number of records by file size: " << n_records_in_file << std::endl;
#endif
    return n_records_in_file;
}

void ranges_by_time_window(
        int **output, int *n_output,
        unsigned long long *input, int n_input,
        double minimum_window_length,
        double maximum_window_length,
        int minimum_number_of_photons_in_time_window,
        int maximum_number_of_photons_in_time_window,
        double macro_time_calibration,
        bool invert
) {
    auto tw_min = (uint64_t) (minimum_window_length / macro_time_calibration);
    auto tw_max = (uint64_t) (maximum_window_length / macro_time_calibration);
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- RANGES BY TIME WINDOW " << std::endl;
    std::clog << "-- minimum_window_length [ms]: " << minimum_window_length << std::endl;
    std::clog << "-- maximum_window_length [ms]: " << maximum_window_length << std::endl;
    std::clog << "-- minimum_number_of_photons_in_time_window: " << minimum_number_of_photons_in_time_window << std::endl;
    std::clog << "-- maximum_number_of_photons_in_time_window: " << maximum_number_of_photons_in_time_window << std::endl;
    std::clog << "-- macro_time_calibration: " << macro_time_calibration << std::endl;
    std::clog << "-- tw_min [macro time clocks]: " << tw_min << std::endl;
    std::clog << "-- tw_max [macro time clocks]: " << tw_max << std::endl;
#endif
    std::vector<int> ss;
    ss.reserve(200);

    size_t tw_begin = 0;
    while (tw_begin < n_input) {
        // search for the end of a time window
        size_t tw_end = tw_begin;
        uint64_t dt;
        for (; tw_end < n_input; tw_end++){
            dt = input[tw_end] - input[tw_begin];
            if(dt >= tw_min) break;
        }
        size_t n_ph = tw_end - tw_begin;
        bool is_selected =
            ((tw_max < 0) || (dt < tw_max)) &&
            ((minimum_number_of_photons_in_time_window < 0) || (n_ph >= minimum_number_of_photons_in_time_window)) &&
            ((maximum_number_of_photons_in_time_window < 0) || (n_ph <= maximum_number_of_photons_in_time_window));
        if(invert) is_selected = !is_selected;
        if (is_selected) {
            ss.push_back(tw_begin);
            ss.push_back(tw_end);
        }
        tw_begin = tw_end;
    }
    *output = (int *) malloc(ss.size() * sizeof(int));
    for(size_t i = 0; i<ss.size(); i++) (*output)[i] = ss[i];
    int n_out = ss.size();
    *n_output = n_out;
}

void selection_by_count_rate(
        int **output, int *n_output,
        unsigned long long *time, int n_time,
        double time_window, int n_ph_max,
        double macro_time_calibration,
        bool invert, bool make_mask
){
    auto tw = (unsigned long) (time_window / macro_time_calibration);
    *output = (int*) calloc(sizeof(int), n_time);
    int i = 0; *n_output = 0;
    while(i < (n_time - 1)){
        int n_ph;
        // start at time[i] and increment r till time[r] - time[i] < tw
        int r = i;
        n_ph = 0;
        while( ((time[r] - time[i]) < tw) && (r < (n_time - 1))){
            r++;
            n_ph++;
        }
        // the right side of the TW is the start for the next time record
        bool select = invert ? (n_ph >= n_ph_max) : (n_ph < n_ph_max);
        if(select){
            for(int k=i; k < r; k++){
                (*n_output)++;
                (*output)[(*n_output) - 1] = k;
            }
        } else if(make_mask){
            for(int k=i; k < r; k++){
                (*n_output)++;
                (*output)[(*n_output) - 1] = -1;
            }
        }
        i = r;
    }
}


std::vector<long long> TTTR::burst_search(int L, int m, double T) {

    int64_t i, i_start, i_stop;
    uint8_t in_burst = 0;

    std::vector<long long> bursts;  // Now storing interleaved start and stop indices
    long long Ti = T / header->get_macro_time_resolution();

    for (i = 0; i <= size() - m; ++i) {
        if (macro_times[i + m - 1] - macro_times[i] <= Ti) {
            if (!in_burst) {
                in_burst = 1;
                i_start = i;
            }
        } else if (in_burst) {
            in_burst = 0;
            i_stop = i + m - 2;
            if (i_stop - i_start + 1 >= L) {
                bursts.push_back(i_start);
                bursts.push_back(i_stop);
            }
        }
    }

    if (in_burst) {
        i_stop = i + m - 1;
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
            this->macro_times, this->n_valid_events,
            time_window_length,
            this->header->get_macro_time_resolution()
    );
}

void compute_intensity_trace(
        int **output, int *n_output,
        unsigned long long *input, int n_input,
        double time_window,
        double macro_time_resolution
){
    int l, r;
    int n_macro_time_clocks = (int) (time_window / macro_time_resolution);
    unsigned long long t_max = input[n_input - 1];
    int n_bin = (int) (t_max / n_macro_time_clocks);

    *n_output = n_bin;
    *output = (int*) calloc(n_bin, sizeof(int));

    l = 0; r = 0;
    while(r < n_input){
        r++;
        int i_bin = int (input[l] / n_macro_time_clocks);
        if ((input[r] - input[l]) > n_macro_time_clocks){
            l = r;
        } else{
            (*output)[i_bin] += 1;
        }
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
        dMT = tttr->macro_times[n] - MT_ov * 4096;
        // Count the number of MT overflows
        MT_ov_last = dMT / 4096;
        // Subtract MT overflows from dMT
        dMT -= MT_ov_last * 4096;
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
            // in the record. Hence we have a valid photon
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
        dMT = tttr->macro_times[n] - MT_ov * T3WRAPAROUND;
        // Count the number of MT overflows
        MT_ov_last = dMT / T3WRAPAROUND;
        // Subtract MT overflows from dMT
        dMT -= MT_ov_last * T3WRAPAROUND;
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
        std::cerr << "ERROR in TTTR::write: combination of "
                     "container and record "
                     "does not make sense." << std::endl;
        return false;
    }
    write_header(filename, header);
    fp = fopen(filename.c_str(), "ab");
    if (fp != nullptr) {
        // append records
        if (record_type == BH_RECORD_TYPE_SPC130) {
            write_spc132_events(fp, this);
        } else if(record_type == PQ_RECORD_TYPE_HHT3v2){
            write_hht3v2_events(fp, this);
        } else{
            std::cerr << "ERROR: Record type "
            << record_type << " not supported" << std::endl;
        }
    } else {
        std::cerr << "ERROR: Cannot write to file: "
        << filename <<  std::endl;
        return false;
    }
    fclose(fp);
    return true;
}

void TTTR::compute_microtime_histogram(
        TTTR *tttr_data,
        double** histogram, int* n_histogram,
        double** time, int* n_time,
        unsigned short micro_time_coarsening,
        std::vector<int> *tttr_indices
) {
    if (tttr_data == nullptr) return;

    // Get resolution information
    /////////////////////////////////
    auto header = *(tttr_data->get_header());
    int n_channels = header.get_number_of_micro_time_channels() / micro_time_coarsening;
    double micro_time_resolution = header.get_micro_time_resolution();

    // get micro times
    ////////////////////////////////
    unsigned short *micro_times; int n_micro_times;
    // tttr_indices -> all micro times
    if(tttr_indices == nullptr){
        tttr_data->get_micro_times(&micro_times, &n_micro_times);
    } else{
        n_micro_times = tttr_indices->size();
        micro_times = (unsigned short*) malloc(sizeof(unsigned short) * n_micro_times);
        for(int i = 0; i < n_micro_times; i++){
            auto mt = tttr_data->micro_times[tttr_indices->data()[i]];
            micro_times[i] = mt;
        }
    }
#ifdef VERBOSE_TTTRLIB
    std::cout << "compute_histogram" << std::endl;
    std::cout << "-- micro_time_coarsening: " << micro_time_coarsening << std::endl;
    std::cout << "-- n_channels: " << n_channels << std::endl;
    std::cout << "-- micro_times[0]: " << micro_times[0] << std::endl;
#endif
    for(int i=0; i<n_micro_times;i++)
        micro_times[i] /= micro_time_coarsening;
#ifdef VERBOSE_TTTRLIB
    std::cout << "-- n_micro_times: " << n_micro_times << std::endl;
    std::cout << "-- micro_times[0]: " << micro_times[0] << std::endl;
#endif

    auto bin_edges = std::vector<unsigned short>(n_channels);
    for (size_t i = 0; i < bin_edges.size(); i++) bin_edges[i] = i;
    auto hist = (double *) malloc(n_channels * sizeof(double));
    for(int i=0; i<n_channels;i++) hist[i] = 0.0;
    histogram1D<unsigned short>(
            micro_times, n_micro_times,
            nullptr, 0,
            bin_edges.data(), bin_edges.size(),
            hist, n_channels,
            "lin", false
    );
    *histogram = hist;
    *n_histogram = n_channels;

    auto t = (double *) malloc(n_channels * sizeof(double));
    for (int i = 0; i < n_channels; i++) t[i] = micro_time_resolution * i * micro_time_coarsening;
    *time = t;
    *n_time = n_channels;
    free(micro_times);
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

    // Scale by background by background fraction
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
#ifdef VERBOSE_TTTRLIB
        std::cout << "-- Appending number of records: " << n_macrotimes << std::endl;
#endif
        size_t n_rec = this->n_valid_events + n_macrotimes;
        this->macro_times = (unsigned long long*) realloc(this->macro_times, n_rec * sizeof(unsigned long long));
        this->micro_times = (unsigned short*) realloc(this->micro_times, n_rec * sizeof(unsigned short));
        this->routing_channels = (signed char*) realloc(this->routing_channels, n_rec * sizeof(signed char));
        this->event_types = (signed char*) realloc(this->event_types, n_rec * sizeof(signed char));
        if(n_valid_events > 0){
            if(shift_macro_time){
                macro_time_offset += this->macro_times[n_valid_events - 1];
            }
        }
        for(int i_rec=0; i_rec < n_macrotimes; i_rec++){
            this->macro_times[i_rec + n_valid_events] = macro_times[i_rec] + macro_time_offset;
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
    append_events(
            other->macro_times, other->n_valid_events,
            other->micro_times, other->n_valid_events,
            other->routing_channels, other->n_valid_events,
            other->event_types, other->n_valid_events,
            shift_macro_time,
            macro_time_offset
    );
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
        t_min = std::min((double) tttr_data->macro_times[i], t_min);
        t_max = std::max((double) tttr_data->macro_times[i], t_max);
    }
    if(macrotime_resolution < 0) {
        macrotime_resolution = tttr_data->header->get_macro_time_resolution();
    }
    auto n = (double) v.size();
    double dT = (t_max - t_min);
#ifdef VERBOSE_TTTRLIB
    std::clog << "COMPUTE_COUNT_RATE" << std::endl;
    std::clog << "-- dT [mT units]:" << dT << std::endl;
    std::clog << "-- number of photons:" << n << std::endl;
    std::clog << "-- macrotime_resolution:" << macrotime_resolution << std::endl;
#endif
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