/****************************************************************************
 * Copyright (C) 2020 by Thomas-Otavio Peulen                               *
 *                                                                          *
 * This file is part of the library tttrlib.                                *
 *                                                                          *
 *   tttrlib is free software: you can redistribute it and/or modify it     *
 *   under the terms of the MIT License.                                    *
 *                                                                          *
 *   tttrlib is distributed in the hope that it will be useful,             *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of         *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.                   *
 *                                                                          *
 ****************************************************************************/

#include <include/record_types.h>
#include <include/tttr.h>
#include <boost/filesystem.hpp>



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
           short *event_types, int n_event_types
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
        std::cerr << "WARNING: The input vectors differ in size. Using " << std::endl;
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
    find_used_routing_channels();
}


TTTR::TTTR(const TTTR &parent, long long *selection, int n_selection) :  TTTR()
        {
#if VERBOSE
    std::clog << "INITIALIZING FROM SELECTION" << std::endl;
#endif
    copy_from(parent, false);
    this->n_valid_events = (size_t) n_selection;
    if ((size_t) n_selection > parent.n_valid_events) {
        std::cerr << "WARNING: The dimension of the selection exceeds the parents dimension." << std::endl;
    }
    allocate_memory_for_records(n_selection);
    for(size_t i=0; i<n_valid_events; i++){
        TTTR::macro_times[i] = parent.macro_times[selection[i]];
        TTTR::micro_times[i] = parent.micro_times[selection[i]];
        TTTR::event_types[i] = parent.event_types[selection[i]];
        TTTR::routing_channels[i] = parent.routing_channels[selection[i]];
    }
    find_used_routing_channels();
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
#if VERBOSE
            std::clog << "-- Number of records: " << n_records_in_file << std::endl;
#endif
            allocate_memory_for_records(n_records_in_file);
            read_records();
            fclose(fp);
        }
#if VERBOSE
            std::clog << "-- Resulting number of TTTR entries: " << n_valid_events << std::endl;
#endif
            return 1;
    } else{
#if VERBOSE
        std::cerr << "-- WARNING: File " << filename << " does not exist" << std::endl;
#endif
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
    if(rewind) fseek(fp, (long) fp_records_begin, SEEK_SET);

    // The data is read in two steps. In the first step bigger data chunks
    // are read. In the second the records are read and processed record by
    // record.
    n_records_read = 0;
    overflow_counter = 0;
    n_valid_events = 0;
    size_t offset = 0;

    bool is_valid_record;
    // read if possible the data in chunks to speed up the access
    char* tmp = (char*) malloc(bytes_per_record * (chunk + 1));
    while(
            (n_records_read < n_rec) &&
            (fread(tmp, bytes_per_record, chunk, fp) == chunk)
            ){
        for(size_t i=0; i<chunk; i++){
            offset = bytes_per_record * i;
            n_valid_events += processRecord(
                    *(uint64_t *)&tmp[offset],
                    overflow_counter,
                    *(uint64_t *)&macro_times[n_valid_events],
                    *(uint32_t *)&micro_times[n_valid_events],
                    *(int16_t *)&routing_channels[n_valid_events],
                    *(int16_t *)&event_types[n_valid_events]
                    );
        }
        n_records_read += chunk;
    }
    // records that do not fit in chunks are read one by one (slower)
    while (
            (n_records_read < n_rec) &&
            fread(&TTTRRecord, (size_t) bytes_per_record, 1, fp)
            )
    {
        n_valid_events += processRecord(
                *(uint64_t *)&tmp[offset],
                overflow_counter,
                *(uint64_t *)&macro_times[n_valid_events],
                *(uint32_t *)&micro_times[n_valid_events],
                *(int16_t *)&routing_channels[n_valid_events],
                *(int16_t *)&event_types[n_valid_events]
        );
        n_records_read += 1;
    }
    free(tmp);
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
#if VERBOSE
        std::cerr << "WARNING: TTTR::header not initialized. Returning empty Header." << std::endl;
#endif
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


int TTTR::get_n_valid_events(){
    return (int) n_valid_events;
}



int TTTR::get_n_events(){
    return (int) n_valid_events;
}



void TTTR::get_selection_by_channel(
        long long **output, int *n_output,
        long long *input, int n_input
){
    ::selection_by_channels(
            output, n_output,
            input, n_input,
            routing_channels, get_n_events()
    );
}


void TTTR::get_selection_by_count_rate(
        long long **output, int *n_output,
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
        int **output, int *n_output,
        int tw_min, int tw_max,
        int n_ph_min, int n_ph_max){

    ranges_by_time_window(
            output, n_output,
            macro_times, (int) n_valid_events,
            tw_min, tw_max,
            n_ph_min, n_ph_max
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
    return n_records_in_file;
}


void ranges_by_time_window(
        int **ranges, int *n_range,
        unsigned long long *time, int n_time,
        int tw_min, int tw_max,
        int n_ph_min, int n_ph_max
) {
    *ranges = (int *) malloc(2 * n_time * sizeof(int));
    *n_range = 0;

    size_t tw_begin = 0;
    while (tw_begin < n_time) {

        // search for the end of a time window
        size_t tw_end = 0;
        for (tw_end = tw_begin; (tw_end < n_time); tw_end++){
            if((time[tw_end] - time[tw_begin]) >= tw_min){
                break;
            }
        }
        size_t n_ph = tw_end - tw_begin;
        unsigned long long dt = time[tw_begin] - time[tw_end];

        if (
                ((tw_max < 0) || (dt < tw_max)) &&
                ((n_ph_min < 0) || (n_ph >= n_ph_min)) &&
                ((n_ph_max < 0) || (n_ph <= n_ph_max))
                ) {
            (*ranges)[(*n_range) + 0] = tw_begin;
            (*ranges)[(*n_range) + 1] = tw_end;
            (*n_range) += 2;
        }
        tw_begin = tw_end;
    }
}


void selection_by_count_rate(
        long long **output, int *n_output,
        unsigned long long *time, int n_time,
        double time_window, int n_ph_max,
        double macro_time_calibration,
        bool invert
){
    auto tw = (unsigned long) (time_window / macro_time_calibration);
    *output = (long long*) calloc(sizeof(long long), n_time);
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


bool TTTR::write_file(
        const char *fn,
        const char* container_type
        ) {
    switch(container_names.left.at(container_type)) {
        case BH_SPC130_CONTAINER:
            fp = fopen(fn, "wb");
            if (fp != nullptr) {

                bh_overflow_t record_overflow;
                record_overflow.bits.invalid = true;
                record_overflow.bits.mtov = true;
                record_overflow.bits.empty = 0;

                bh_spc130_record_t record;
                record.bits.invalid = false;
                record.bits.mtov = false;

                int n = 0;
                unsigned long MT_ov = 0;
                while (n < n_valid_events) {
                    unsigned long MT = macro_times[n] - MT_ov * 4095;
                    if (MT > 4095) {
                        /* invalid photon */
                        unsigned long MT_ov_last = MT / (4096);
                        record_overflow.bits.cnt = MT_ov_last;
                        fwrite(&record_overflow, 4, 1, fp);
                        MT_ov += MT_ov_last;
                    } else {
                        /* valid photon */
                        record.bits.adc = 4095 - micro_times[n];
                        record.bits.rout = routing_channels[n];
                        record.bits.mt = MT;
                        fwrite(&record, 1, 4, fp);
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

