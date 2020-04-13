#include <include/record_types.h>
#include <include/tttr.h>
#include <boost/filesystem.hpp>

#define MIN(a,b) ((a) < (b) ? (a) : (b))
#define MAX(a,b) ((a) > (b) ? (a) : (b))


TTTR::TTTR() :
        // private
        filename(),
        header(nullptr),
        tttr_container_type(-1),
        tttr_container_type_str(),
        tttr_record_type(-1),
        overflow_counter(0),
        bytes_per_record(4),
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
    container_names.insert({std::string("PTU"), 0});
    container_names.insert({std::string("HT3"), 1});
    container_names.insert({std::string("SPC-130"), 2});
    container_names.insert({std::string("SPC-600_256"), 3});
    container_names.insert({std::string("SPC-600_4096"), 4});
    container_names.insert({std::string("PHOTON-HDF5"), 5});
    header = new Header();
}


TTTR::TTTR(unsigned long long *macro_times, int n_macrotimes,
           unsigned int *micro_times, int n_microtimes,
           short *routing_channels, int n_routing_channels,
           short *event_types, int n_event_types,
           bool find_used_channels
): TTTR() {
#if VERBOSE
    std::clog << "INITIALIZING FROM VECTORS" << std::endl;
#endif
    this->filename = "NA";
    size_t n_elements = 0;
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
        long long *selection,
        int n_selection,
        bool find_used_channels) :  TTTR()
        {
#if VERBOSE
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
    header = new Header(*p2.header);
    tttr_container_type = p2.tttr_container_type;
    tttr_container_type_str = p2.tttr_container_type_str;
    bytes_per_record = p2.bytes_per_record;
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


TTTR::TTTR(
        const char *fn,
        int container_type,
        bool read_input
        ) :
        TTTR()
{
    filename.assign(fn);
    tttr_container_type = container_type;
    if(read_input){
        read_file();
    }
    find_used_routing_channels();
}


TTTR::TTTR(const char *fn, int container_type) :
    TTTR(fn, container_type, true) {
    tttr_container_type_str.assign(
            container_names.right.at(container_type)
            );
}


TTTR::TTTR(const char *fn, const char *container_type) :
        TTTR() {
    tttr_container_type_str.assign(container_type);
    tttr_container_type = container_names.left.at(std::string(container_type));
    filename.assign(fn);
    read_file();
    find_used_routing_channels();
}

void TTTR::shift_macro_time(int shift) {
    for(size_t i=0; i<n_valid_events; i++){
        macro_times[i] += shift;
    }
}

void TTTR::find_used_routing_channels(){
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
#if VERBOSE
    std::clog << "-- Used routing channels: ";
    for(auto c: used_routing_channels){
        std::clog << c << ", ";
    }
    std::clog << std::endl;
#endif
}

int TTTR::read_hdf_file(const char *fn){
    header = new Header();

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
#if VERBOSE
    std::clog << "n_records_in_file: " << n_records_in_file << std::endl;
#endif

    /*
     * Read the data
     */
    H5Dread(
            ds_microtime,
            H5T_NATIVE_UINT32, H5S_ALL, H5S_ALL, H5P_DEFAULT,
            micro_times
    );
    H5Dread(
            ds_n_sync_pulses,
            H5T_NATIVE_UINT64, H5S_ALL, H5S_ALL, H5P_DEFAULT,
            macro_times
    );
    H5Dread(
            ds_routing_channels,
            H5T_NATIVE_INT16, H5S_ALL, H5S_ALL, H5P_DEFAULT,
            routing_channels
    );

    /* close file */
    H5Fclose(hdf5_file);
    H5Sclose(space);
    return 1;
}


int TTTR::read_file(
        const char *fn,
        int container_type
        ) {
#if VERBOSE
    std::clog << "READING TTTR FILE" << std::endl;
#endif
    if(boost::filesystem::exists(fn))
    {
#if VERBOSE
        std::clog << "-- Filename: " << fn << std::endl;
        std::clog << "-- Container type: " << container_type << std::endl;
#endif
        // clean up filename
        boost::filesystem::path p = fn;
        filename = boost::filesystem::canonical(boost::filesystem::absolute(p)).generic_string();
        fn = filename.c_str();
        if (container_type == PHOTON_HDF_CONTAINER) {
            read_hdf_file(fn);
        } else{
            fp = fopen(fn, "rb");
            header = new Header(fp, container_type);
            fp_records_begin = header->header_end;
            bytes_per_record = header->bytes_per_record;
            tttr_record_type = header->getTTTRRecordType();
#if VERBOSE
            std::clog << "-- TTTR record type: " << tttr_record_type << std::endl;
#endif
            processRecord = processRecord_map[tttr_record_type];
            n_records_in_file = determine_number_of_records_by_file_size(
                    fp,
                    header->header_end,
                    bytes_per_record
            );
            allocate_memory_for_records(n_records_in_file);
            read_records();
            fclose(fp);
        }
#if VERBOSE
            std::clog << "-- Resulting number of TTTR entries: " << n_valid_events << std::endl;
#endif
            return 1;
    } else{
        std::clog << "-- WARNING: File " << filename << " does not exist" << std::endl;
        return 0;
    }
}


int TTTR::read_file(){
    return read_file(filename.c_str(), tttr_container_type);
}


TTTR::~TTTR() {
    delete header;
    deallocate_memory_of_records();
}


std::string TTTR::get_filename() {
    return std::string(filename);
}


void TTTR::allocate_memory_for_records(size_t n_rec){
#if VERBOSE
    std::clog << "-- Allocating memory for " << n_rec << " TTTR records." << std::endl;
#endif
    if(tttr_container_type == 5) {
        macro_times = (unsigned long long*) H5allocate_memory(
                n_rec * sizeof(unsigned long long), false
                );
        micro_times = (unsigned int*) H5allocate_memory(
                n_rec * sizeof(unsigned int), false
                );
        routing_channels = (short*) H5allocate_memory(
                n_rec * sizeof(short), false
                );
        event_types = (short*) H5allocate_memory(
                n_rec * sizeof(short), false
                );
    } else {
        macro_times = (unsigned long long*) malloc(
                n_rec * sizeof(unsigned long long)
                );
        micro_times = (unsigned int*) malloc(
                n_rec * sizeof(unsigned int)
                );
        routing_channels = (short*) malloc(
                n_rec * sizeof(short)
                );
        event_types = (short*) malloc(
                n_rec * sizeof(short)
                );
    }
}


void TTTR::deallocate_memory_of_records(){
    if(tttr_container_type == PHOTON_HDF_CONTAINER) {
        H5free_memory(macro_times);
        H5free_memory(routing_channels);
        H5free_memory(micro_times);
        H5free_memory(event_types);
        H5garbage_collect();

    } else {
        free(macro_times);
        free(routing_channels);
        free(micro_times);
        free(event_types);
    }
}


void TTTR::read_records(
        size_t n_rec,
        bool rewind,
        size_t chunk
        ) {
    n_rec = n_rec < n_records_in_file ? n_rec : n_records_in_file;
#if VERBOSE
    std::cout << "-- Records that will be read : " << n_rec << std::endl;
#endif
    if(rewind) fseek(fp, (long) fp_records_begin, SEEK_SET);

    // The data is read in two steps. In the first step bigger data chunks
    // are read. In the second the records are read and processed record by
    // record.
    n_records_read = 0;
    overflow_counter = 0;
    n_valid_events = 0;
    size_t offset = 0;

    // read if possible the data in chunks to speed up the access
    size_t number_of_objects;
    do{
        char* tmp = (char*) malloc(bytes_per_record * (chunk + 1));
        number_of_objects = fread(tmp, bytes_per_record, chunk, fp);
        for (size_t j = 0; j < number_of_objects; j++) {
            offset = bytes_per_record * j;
            n_valid_events += processRecord(
                    *(uint64_t *) &tmp[offset],
                    overflow_counter,
                    *(uint64_t *) &macro_times[n_valid_events],
                    *(uint32_t *) &micro_times[n_valid_events],
                    *(int16_t *) &routing_channels[n_valid_events],
                    *(int16_t *) &event_types[n_valid_events]
            );
        }
        free(tmp);
        n_records_read += number_of_objects;
    }while(number_of_objects > 0);
}


void TTTR::read_records(size_t n_rec){
    read_records(n_rec, true, 16384);
}


void TTTR::read_records() {
    read_records(n_records_in_file);
}


Header TTTR::get_header() {
#if VERBOSE
    std::clog << "-- TTTR::get_header" << std::endl;
#endif
    if(header != nullptr){
        return *header;
    } else{
        std::clog << "WARNING: TTTR::header not initialized. Returning empty Header." << std::endl;
        return Header();
    }
}


void TTTR::get_macro_time(unsigned long long** output, int* n_output){
    get_array<unsigned long long>(n_valid_events, macro_times, output, n_output);
}


void TTTR::get_micro_time(uint32_t** output, int* n_output){
    get_array<uint32_t>(n_valid_events, micro_times, output, n_output);
}


void TTTR::get_routing_channel(int16_t** output, int* n_output){
    get_array<int16_t>(n_valid_events, routing_channels, output, n_output);
}


void TTTR::get_used_routing_channels(int16_t** output, int* n_output){
    get_array(
            used_routing_channels.size(),
            used_routing_channels.data(),
            output,
            n_output
    );
}


void TTTR::get_event_type(int16_t** output, int* n_output){
    get_array(
            n_valid_events,
            event_types,
            output,
            n_output
    );
}


unsigned int TTTR::get_n_valid_events(){
    return (int) n_valid_events;
}



unsigned int TTTR::get_n_events(){
    return (int) n_valid_events;
}



void TTTR::get_selection_by_channel(
        long long **output, int *n_output,
        long long *input, int n_input){
    ::selection_by_channels(
            output, n_output,
            input, n_input,
            routing_channels, get_n_events()
    );
}


void TTTR::get_selection_by_count_rate(
        unsigned long long **output, int *n_output,
        double time_window, int n_ph_max,
        bool invert
){
    selection_by_count_rate(
            output, n_output,
            macro_times, (int) n_valid_events,
            time_window, n_ph_max,
            header->macro_time_resolution,
            invert
    );
}


void TTTR::get_ranges_by_count_rate(
        unsigned long long **output, int *n_output,
        double minimum_window_length,
        double maximum_window_length,
        int minimum_number_of_photons_in_time_window,
        int maximum_number_of_photons_in_time_window,
        bool invert
        ){
    // macro_time_resolution is in ns minimum_window_length selection
    // is in units of ms
    double macro_time_calibration = header->macro_time_resolution / 1e6;
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


TTTR* TTTR::select(long long *selection, int n_selection) {
    return new TTTR(*this, selection, n_selection);
}


void selection_by_channels(
        long long **output, int *n_output,
        long long *input, int n_input,
        short *routing_channels, int n_routing_channels) {
    size_t n_sel;
    *output = (long long *) malloc(n_routing_channels * sizeof(long long));

    n_sel = 0;
    for (long long i = 0; i < n_routing_channels; i++) {
        int ch = routing_channels[i];
        for (int j = 0; j < n_input; j++) {
            if (input[j] == ch) {
                (*output)[n_sel++] = i;
            }
        }
    }
    *n_output = (int) n_sel;
}


size_t determine_number_of_records_by_file_size(
        std::FILE *fp,
        size_t offset,
        size_t bytes_per_record
        ){
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
#if VERBOSE
    std::clog << "-- Number of records by file size: " << n_records_in_file << std::endl;
#endif
    return n_records_in_file;
}


void ranges_by_time_window(
        unsigned long long **output, int *n_output,
        unsigned long long *input, int n_input,
        double minimum_window_length,
        double maximum_window_length,
        int minimum_number_of_photons_in_time_window,
        int maximum_number_of_photons_in_time_window,
        double macro_time_calibration,
        bool invert
) {
    auto tw_min = (unsigned long) (minimum_window_length / macro_time_calibration);
    auto tw_max = (unsigned long) (maximum_window_length / macro_time_calibration);
    *output = (unsigned long long *) malloc(2 * n_input * sizeof(unsigned long long));
    *n_output = 0;

    size_t tw_begin = 0;
    while (tw_begin < n_input) {

        // search for the end of a time window
        size_t tw_end = 0;
        for (tw_end = tw_begin; (tw_end < n_input); tw_end++){
            if((input[tw_end] - input[tw_begin]) >= tw_min){
                break;
            }
        }
        size_t n_ph = tw_end - tw_begin;
        unsigned long long dt = input[tw_begin] - input[tw_end];
        bool is_selected =
                ((tw_max < 0) || (dt < tw_max)) &&
                ((minimum_number_of_photons_in_time_window < 0) || (n_ph >= minimum_number_of_photons_in_time_window)) &&
                ((maximum_number_of_photons_in_time_window < 0) || (n_ph <= maximum_number_of_photons_in_time_window));
        is_selected = invert ? !is_selected : is_selected;
        if (is_selected) {
            (*output)[(*n_output) + 0] = tw_begin;
            (*output)[(*n_output) + 1] = tw_end;
            (*n_output) += 2;
        }
        tw_begin = tw_end;
    }
}


void selection_by_count_rate(
        unsigned long long **output, int *n_output,
        unsigned long long *time, int n_time,
        double time_window, int n_ph_max,
        double macro_time_calibration,
        bool invert
){
    auto tw = (unsigned long) (time_window / macro_time_calibration);
    *output = (unsigned long long*) calloc(sizeof(unsigned long long), n_time);
    int i = 0;
    *n_output = 0;
    while (i < (n_time - 1)){
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
        }
        i = r;
    }
}


unsigned int TTTR::get_number_of_micro_time_channels(){
    return header->get_effective_number_of_micro_time_channels();
}


void histogram_trace(
        int **output, int *n_output,
        unsigned long long *input, int n_input,
        int time_window){
    int l, r;
    unsigned long long t_max = input[n_input - 1];

    int n_bin = (int) (t_max / time_window);

    *n_output = n_bin;
    *output = (int*) calloc(n_bin, sizeof(int));

    l = 0; r = 0;
    while(r < n_input){
        r++;
        int i_bin = int (input[l] / time_window);
        if ((input[r] - input[l]) > time_window){
            l = r;
        } else{
            (*output)[i_bin] += 1;
        }
    }
}


void get_ranges_channel(
        int **ranges, int *n_range,
        short *channel, int n_channel,
        int selection_channel
        ){
    *n_range = 0;
    *ranges = (int *) malloc(2 * n_channel * sizeof(int));

    int previous_marker_position = 0;
    int next_marker_position;
    int i = 0;

    // find first marker position
    for(i=0; i<n_channel; i++){
        if(channel[i] == selection_channel){
            previous_marker_position = i;
            break;
        }
    }

    while(i<n_channel)
    {
        // find next marker position
        for(; i<n_channel; i++) {
            if (channel[i] == selection_channel) {
                next_marker_position = i;
                *ranges[2 * (*n_range) + 0] = previous_marker_position;
                *ranges[2 * (*n_range) + 1] = next_marker_position;
                *n_range += 1;
                previous_marker_position = next_marker_position;
                break;
            }
        }
    }

}

bool TTTR::write(
        const char *fn,
        const char* container_type
        ) {
    switch(container_names.left.at(container_type)) {
        case BH_SPC130_CONTAINER:
            fp = fopen(fn, "wb");
            if (fp != nullptr) {
                // write header
                bh_spc132_header_t head;
                head.invalid = true;
                head.macro_time_clock = (unsigned) (header->macro_time_resolution * 10.0);
                fwrite(&head, 4, 1, fp);

                bh_overflow_t overflow;
                overflow.bits.empty = 0;
                overflow.bits.mtov = 1;
                overflow.bits.invalid = 1;

                bh_spc130_record_t record;
                unsigned dMT;
                unsigned long long MT_ov_last = 0;
                unsigned long long MT_ov = 0;
                for(size_t n = 0; n < get_n_valid_events();){
                    // time since last macro_time record
                    dMT = macro_times[n] - MT_ov * 4096;
                    // Count the number of MT overflows
                    MT_ov_last = dMT / 4096;
                    // increment the global overflow counter
                    MT_ov += MT_ov_last;

                    // write overflows
                    while(MT_ov_last>1){
                        // we fit 65536 = 2**16 in each overflow record
                        overflow.bits.cnt = MIN(65536, MT_ov_last);
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
                        n++;
                    }
                }
                fclose(fp);
            } else {
                std::cerr << "Cannot write to file: " << filename <<  std::endl;
                return false;
            }
            break;
    }
    return true;
}

TTTRRange::TTTRRange(const TTTRRange& p2){
//#if VERBOSE
//    std::clog << "TTTRRange copy" << std::endl;
//#endif
    _start = p2._start;
    _stop = p2._stop;
    _start_time = p2._start_time;
    _stop_time = p2._stop_time;
    for(auto &v: p2._tttr_indices){
        _tttr_indices.emplace_back(v);
    }
}

TTTRRange::TTTRRange(
        size_t start,
        size_t stop,
        long long start_time,
        long long stop_time,
        TTTRRange* other
) {
    if(other != nullptr){
        this->_start = other->_start;
        this->_stop = other->_stop;
        this->_start_time = other->_start_time;
        this->_stop_time = other->_stop_time;
    } else {
        this->_start = start;
        this->_stop = stop;
        this->_start_time = start_time;
        this->_stop_time = stop_time;
    }
}

