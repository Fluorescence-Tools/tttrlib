/****************************************************************************
 * Copyright (C) 2019 by Thomas-Otavio Peulen                               *
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
        filename(nullptr),
        header(nullptr),
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
}


TTTR::TTTR(
        unsigned long long *n_sync_pulses,
        unsigned int *micro_times,
        short *routing_channels,
        int16_t *event_types) :
        TTTR() {
    TTTR::macro_times = n_sync_pulses;
    TTTR::micro_times = micro_times;
    TTTR::routing_channels = routing_channels;
    TTTR::event_types = event_types;
}


TTTR::TTTR(
        TTTR *parent,
        long long *selection,
        int n_selection
) :
        TTTR()
        {
    TTTR::n_valid_events = (size_t) n_selection;

    if ((size_t) n_selection > parent->n_valid_events) {
        // the selection does not match the dimension of the parent
        throw std::invalid_argument("The dimension of "
                                    "the selection exceeds "
                                    "the parents dimension.");
    }

    allocate_memory_for_records(n_valid_events);
    for(size_t i=0; i<n_valid_events; i++){
        TTTR::macro_times[i] = parent->macro_times[selection[i]];
        TTTR::micro_times[i] = parent->micro_times[selection[i]];
        TTTR::event_types[i] = parent->event_types[selection[i]];
        TTTR::routing_channels[i] = parent->routing_channels[selection[i]];
    }
}


TTTR::TTTR(
        char *fn,
        int container_type,
        bool read_input
        ) :
        TTTR()
{
    filename = fn;
    tttr_container_type = container_type;
    if(read_input){
        read_file();
    }
    find_used_routing_channels();
}


TTTR::TTTR(char *fn, int container_type) :
    TTTR(fn, container_type, true) {
}


TTTR::TTTR(char *fn, const char *container_type) :
        TTTR() {
    tttr_container_type = container_names[std::string(container_type)];
    filename = fn;
    read_file();
    find_used_routing_channels();
}

void TTTR::shift_macro_time(unsigned int shift) {
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
    std::clog << "-- Used routing channels: ";
    for(auto c: used_routing_channels){
        std::clog << c << ", ";
    }
    std::clog << std::endl;
}

int TTTR::read_hdf_file(char *fn){
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
    H5Sget_simple_extent_dims(space, dims, NULL);
    allocate_memory_for_records(dims[0]);
    /* All records are assumed to be valid.
     * Invalid records are usually sorted out
     * before storing the hdf5 file. */
    n_valid_events = dims[0];
    n_records_in_file = dims[0];

    std::clog << "n_records_in_file: " << n_records_in_file << std::endl;

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
        char *fn,
        int container_type
        ) {
    std::clog << "Reading TTTR file" << std::endl;
    std::clog << "-- Filename: " << fn << std::endl;
    std::clog << "-- Container type: " << container_type << std::endl;
    if(boost::filesystem::exists(fn))
    {
        if (container_type == PHOTON_HDF_CONTAINER)
        {
            read_hdf_file(fn);
        }
        else{
            fp = fopen(fn, "rb");
            if (fp != nullptr)
            {
                header = new Header(fp, container_type);
                fp_records_begin = header->header_end;
                bytes_per_record = header->bytes_per_record;
                tttr_record_type = header->getTTTRRecordType();
                std::clog << "-- TTTR record type: " << tttr_record_type << std::endl;
                processRecord = processRecord_map[tttr_record_type];
                n_records_in_file = determine_number_of_records_by_file_size(
                        fp,
                        header->header_end,
                        bytes_per_record
                );
                std::clog << "-- Number of records: " << n_records_in_file <<
                std::endl;
            }
            else{
                std::clog << "-- WARNING: File " << filename << " does not exist" << std::endl;
                return 0;
            }
            allocate_memory_for_records(n_records_in_file);
            read_records();
            fclose(fp);
        }
        return 1;
    }
    std::cerr << "-- WARNING: Could not open file " << fn << std::endl;
    return 1;
}


int TTTR::read_file(){
    return read_file(filename, tttr_container_type);
}


TTTR::~TTTR() {
    delete header;
    deallocate_memory_of_records();
}


char* TTTR::get_filename() {
    return filename;
}


void TTTR::allocate_memory_for_records(size_t n_rec){
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
            is_valid_record = processRecord(
                    *(uint64_t *)&tmp[offset],
                    overflow_counter,
                    *(uint64_t *)&macro_times[n_valid_events],
                    *(uint32_t *)&micro_times[n_valid_events],
                    *(int16_t *)&routing_channels[n_valid_events],
                    *(int16_t *)&event_types[n_valid_events]
                    );
            n_valid_events += is_valid_record;
//            if(is_valid_record){
//                n_valid_events++;
//            }
        }
        n_records_read += chunk;
    }

    // records that do not fit in chunks are read one by one (slower)
    while (
            (n_records_read < n_rec) &&
            fread(&TTTRRecord, (size_t) bytes_per_record, 1, fp)
            )
    {
        is_valid_record = processRecord(
                *(uint64_t *)&tmp[offset],
                overflow_counter,
                *(uint64_t *)&macro_times[n_valid_events],
                *(uint32_t *)&micro_times[n_valid_events],
                *(int16_t *)&routing_channels[n_valid_events],
                *(int16_t *)&event_types[n_valid_events]
        );
//        if(is_valid_record){
//            n_valid_events++;
//        }
        n_valid_events += is_valid_record;
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
    return *header;
}


void TTTR::get_macro_time(unsigned long long** out, int* n_out){
    get_array(
            n_valid_events,
            macro_times,
            out,
            n_out
    );
}


void TTTR::get_micro_time(uint32_t** out, int* n_out){
    get_array(
            n_valid_events,
            micro_times,
            out,
            n_out
    );
}


void TTTR::get_routing_channel(int16_t** out, int* n_out){
    get_array(
            n_valid_events,
            routing_channels,
            out,
            n_out
    );
}

void TTTR::get_used_routing_channels(int16_t** out, int* n_out){
    get_array(
            used_routing_channels.size(),
            used_routing_channels.data(),
            out,
            n_out
    );
}


void TTTR::get_event_type(int16_t** out, int* n_out){
    get_array(
            n_valid_events,
            event_types,
            out,
            n_out
    );
}


int TTTR::get_n_valid_events(){
    return (int) n_valid_events;
}



int TTTR::get_n_events(){
    return (int) n_valid_events;
}



void TTTR::get_selection_by_channel(
        long long **out, int *n_out,
        long long *in, int n_in
){
    ::selection_by_channels(
            out, n_out,
            in, n_in,
            routing_channels, get_n_events()
    );
}


void TTTR::get_selection_by_count_rate(
        long long **out, int *n_out,
        unsigned long tw, int n_ph_max
){
    selection_by_count_rate(
            out, n_out,
            macro_times, (int) n_valid_events,
            tw, n_ph_max
            );
}


void TTTR::get_ranges_by_count_rate(
        int **out, int *n_out,
        int tw_min, int tw_max,
        int n_ph_min, int n_ph_max){

    ranges_by_time_window(
            out, n_out,
            macro_times, (int) n_valid_events,
            tw_min, tw_max,
            n_ph_min, n_ph_max
            );

}


TTTR* TTTR::select(long long *selection, int n_selection) {
    return new TTTR(this, selection, n_selection);
}


void selection_by_channels(
        long long **out, int *n_out,
        long long *in, int n_in,
        short *routing_channels, int n_routing_channels) {
    size_t n_sel;
    *out = (long long *) malloc(n_routing_channels * sizeof(long long));

    n_sel = 0;
    for (long long i = 0; i < n_routing_channels; i++) {
        int ch = routing_channels[i];
        for (int j = 0; j < n_in; j++) {
            if (in[j] == ch) {
                (*out)[n_sel++] = i;
            }
        }
    }
    *n_out = (int) n_sel;
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
        long long **out, int *n_out,
        unsigned long long *time, int n_time,
        unsigned long tw, int n_ph_max
){
    *out = (long long*) calloc(sizeof(long long), n_time);
    int i = 0;
    *n_out = 0;
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
        if(n_ph < n_ph_max){
            for(int k=i; k < r; k++){
                (*n_out)++;
                (*out)[(*n_out) - 1] = k;
            }
        }
        i = r;
    }
}


unsigned int TTTR::get_number_of_tac_channels(){
    return header->number_of_tac_channels;
}


void histogram_trace(
        int **out, int *n_out,
        unsigned long long *in, int n_in,
        int time_window){
    int l, r;
    unsigned long long t_max = in[n_in - 1];

    int n_bin = (int) (t_max / time_window);

    *n_out = n_bin;
    *out = (int*) malloc(n_bin * sizeof(int));
    memset(out, 0, n_bin * sizeof(int));

    l = 0; r = 0;
    while(r < n_in){
        r++;
        int i_bin = int (in[l] / time_window);
        if ((in[r] - in[l]) > time_window){
            l = r;
        } else{
            (*out)[i_bin] += 1;
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


std::vector<size_t > TTTRRange::get_tttr_indices() {
    std::vector<size_t > v;
    for(size_t i=start; i<stop; i++){
        v.emplace_back(i);
    }
    return v;
}


bool TTTR::write_file(
        char *fn,
        const char* container_type
        ) {
    switch(container_names[container_type]) {
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

