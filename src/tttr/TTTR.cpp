#include "include/TTTR.h"
#include "TTTRRange.h"


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
    header = new TTTRHeader();
}

TTTR::TTTR(unsigned long long *macro_times, int n_macrotimes,
           unsigned int *micro_times, int n_microtimes,
           short *routing_channels, int n_routing_channels,
           short *event_types, int n_event_types,
           bool find_used_channels
): TTTR() {
#if VERBOSE_TTTRLIB
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
#if VERBOSE_TTTRLIB
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
        if(read_file()) find_used_routing_channels();
    }
}

TTTR::TTTR(const char *fn, int container_type) :
    TTTR(fn, container_type, true) {
    try {
        tttr_container_type_str.assign(
                container_names.right.at(container_type)
        );
    }
    catch(...) {
        std::cerr << "Container type " << container_type
        << " not supported." << std::endl;
    }
}

TTTR::TTTR(const char *fn, const char *container_type) :
        TTTR() {
    try {
        tttr_container_type_str.assign(container_type);
        tttr_container_type = container_names.left.at(std::string(container_type));
        filename.assign(fn);
        if(read_file()) find_used_routing_channels();
    }
    catch(...) {
        std::cerr << "Container type " << container_type
        << " not supported." << std::endl;
    }
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
#if VERBOSE_TTTRLIB
    std::clog << "-- Used routing channels: ";
    for(auto c: used_routing_channels){
        std::clog << static_cast<unsigned>(c) << ", ";
    }
    std::clog << std::endl;
#endif
}

int TTTR::read_hdf_file(const char *fn){
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
#if VERBOSE_TTTRLIB
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
}

int TTTR::read_file(
        const char *fn,
        int container_type
        ) {
#if VERBOSE_TTTRLIB
    std::clog << "READING TTTR FILE" << std::endl;
#endif
    if(fn == nullptr){
        fn = filename.c_str();
    }
    if(container_type < 0){
        container_type = tttr_container_type;
    }

    if(boost::filesystem::exists(fn))
    {
#if VERBOSE_TTTRLIB
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
            header = new TTTRHeader(fp, container_type);
            fp_records_begin = header->header_end;
            bytes_per_record = header->bytes_per_record;
            tttr_record_type = header->getTTTRRecordType();
#if VERBOSE_TTTRLIB
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
#if VERBOSE_TTTRLIB
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
#if VERBOSE_TTTRLIB
    std::clog << "-- Allocating memory for " << n_rec << " TTTR records." << std::endl;
#endif
    if(tttr_container_type == 5) {
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
    } else {
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
#if VERBOSE_TTTRLIB
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

    // read if possible the data in chunks to speed up the access
    size_t number_of_objects;
    do{
        signed char* tmp = (signed char*) malloc(bytes_per_record * (chunk + 1));
        number_of_objects = fread(tmp, bytes_per_record, chunk, fp);
        for (size_t j = 0; j < number_of_objects; j++) {
            offset = bytes_per_record * j;
            n_valid_events += processRecord(
                    *(uint32_t *) &tmp[offset],
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

TTTRHeader TTTR::get_header() {
#if VERBOSE_TTTRLIB
    std::clog << "-- TTTR::get_header" << std::endl;
#endif
    if(header != nullptr){
        return *header;
    } else{
        std::clog << "WARNING: TTTR::header not initialized. Returning empty Header." << std::endl;
        return TTTRHeader();
    }
}

void TTTR::get_macro_time(unsigned long long** output, int* n_output){
    get_array<unsigned long long>(n_valid_events, macro_times, output, n_output);
}

void TTTR::get_micro_time(unsigned short** output, int* n_output){
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
        int *input, int n_input){
    selection_by_channels(
            output, n_output,
            input, n_input,
            routing_channels, get_n_events()
    );
}

void TTTR::get_selection_by_count_rate(
        int **output, int *n_output,
        double time_window, int n_ph_max,
        bool invert
){
    selection_by_count_rate(
            output, n_output,
            macro_times, (int) n_valid_events,
            time_window, n_ph_max,
            header->macro_time_resolution / 1e6,
            invert
    );
}

void TTTR::get_time_window_ranges(
        int **output, int *n_output,
        double minimum_window_length,
        int minimum_number_of_photons_in_time_window,
        int maximum_number_of_photons_in_time_window,
        double maximum_window_length,
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

TTTR* TTTR::select(int *selection, int n_selection) {
    return new TTTR(*this, selection, n_selection);
}

void selection_by_channels(
        int **output, int *n_output,
        int *input, int n_input,
        signed char *routing_channels, int n_routing_channels) {
    size_t n_sel;
    *output = (int *) malloc(n_routing_channels * sizeof(int));
    n_sel = 0;
    for (int i = 0; i < n_routing_channels; i++) {
        int ch = routing_channels[i];
        for (int j = 0; j < n_input; j++) {
            if (input[j] == ch) {
                (*output)[n_sel++] = i;
            }
        }
    }
    *n_output = (int) n_sel;
}

size_t TTTR::determine_number_of_records_by_file_size(
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
#if VERBOSE_TTTRLIB
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
    auto tw_min = (unsigned long) (minimum_window_length / macro_time_calibration);
    auto tw_max = (unsigned long) (maximum_window_length / macro_time_calibration);
#if VERBOSE_TTTRLIB
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
        size_t tw_end;
        unsigned long long dt;
        for (tw_end = tw_begin; tw_end < n_input; tw_end++){
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
        bool invert
){
    auto tw = (unsigned long) (time_window / macro_time_calibration);
    *output = (int*) calloc(sizeof(int), n_time);

    int i = 0; *n_output = 0;
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

void TTTR::intensity_trace(
        int **output, int *n_output,
        double time_window_length
){
    compute_intensity_trace(
            output, n_output,
            this->macro_times, this->n_valid_events,
            time_window_length,
            this->header->macro_time_resolution / 1e6
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

void get_ranges_channel(
        unsigned int **ranges, int *n_range,
        short *channel, int n_channel,
        int selection_channel
        ){
    *n_range = 0;
    *ranges = (unsigned int *) malloc(2 * n_channel * sizeof(unsigned int));

    int previous_marker_position = 0;
    int next_marker_position;
    int i;
    // find first marker position
    for(i=0; i<n_channel; i++){
        if(channel[i] == selection_channel){
            previous_marker_position = i;
            break;
        }
    }
    while(i<n_channel){
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
        const char *filename,
        const char* container_type
        ) {
    switch(container_names.left.at(container_type)) {
        case BH_SPC130_CONTAINER:
            fp = fopen(filename, "wb");
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
                unsigned long long MT_ov_last;
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

void TTTR::compute_microtime_histogram(
        TTTR *tttr_data,
        double** histogram, int* n_histogram,
        double** time, int* n_time,
        unsigned short micro_time_coarsening
) {
    // construct histogram
    if (tttr_data != nullptr) {
        auto header = tttr_data->get_header();
        int n_channels = header.number_of_micro_time_channels / micro_time_coarsening;
        double micro_time_resolution = header.micro_time_resolution;
        unsigned short *micro_times; int n_micro_times;
        tttr_data->get_micro_time(&micro_times, &n_micro_times);
#if VERBOSE_TTTRLIB
        std::cout << "compute_histogram" << std::endl;
        std::cout << "-- micro_time_coarsening: " << micro_time_coarsening << std::endl;
        std::cout << "-- n_channels: " << n_channels << std::endl;
        std::cout << "-- micro_times[0]: " << micro_times[0] << std::endl;
#endif
        for(int i=0; i<n_micro_times;i++)
            micro_times[i] /= micro_time_coarsening;
#if VERBOSE_TTTRLIB
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
}

double TTTR::compute_mean_lifetime(
        TTTR* tttr_data,
        TTTR* tttr_irf,
        int m0_irf, int m1_irf
){
    if(tttr_irf != nullptr){
        unsigned short *micro_times_irf; int n_micro_times_irf;
        tttr_irf->get_micro_time(&micro_times_irf, &n_micro_times_irf);
        m0_irf = n_micro_times_irf;
        m1_irf = 0;
        for(int i=0; i< n_micro_times_irf; i++) m1_irf += micro_times_irf[i];
    }

    unsigned short *micro_times_data; int n_micro_times_data;
    tttr_data->get_micro_time(&micro_times_data, &n_micro_times_data);
    double mu0 = n_micro_times_data;
    double mu1 = 0.0;
    for(int i=0; i< n_micro_times_data; i++) mu1 += micro_times_data[i];

    double g1 = mu0 / m0_irf;
    double g2 = (mu1 - g1 * m1_irf) / m0_irf;
    double tau1 = g2 / g1;
    return tau1 * tttr_data->get_header().micro_time_resolution;
}