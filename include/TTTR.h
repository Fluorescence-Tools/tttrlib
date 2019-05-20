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

#define _CRT_SECURE_NO_DEPRECATE

#ifndef TTTRLIB_TTTR_H
#define TTTRLIB_TTTR_H


#include <stdint.h>
#include <string>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>
#include <stdio.h>
#include <map>
#include <array>
#include <memory>
#include "hdf5.h"
#include "H5Tpublic.h"

#include <RecordReader.h>
#include <Header.h>


#define RECORD_PHOTON               0
#define RECORD_MARKER               1


/*!
 * Determines the number of records in a TTTR files (for use with not HDF5)
 *
 * Calculates the number of records in the file based on the file size.
 * if @param offset is passed the number of records is calculated by the file size
 * the number of bytes in the file - offset and @param bytes_per_record.
 * If @param offset is not specified the current location of the file pointer
 * is used as an offset. If @param bytes_per_record is not specified
 * the attribute value bytes_per_record of the class instance is used.
 *
 * @param offset
 * @param bytes_per_record
 */
size_t determine_number_of_records_by_file_size(
        std::FILE *fp,
        size_t offset,
        size_t bytes_per_record
);



/*!
 * A count rate (cr) filter that returns an array containing a list of indices where
 * the cr was smaller than a specified cr.
 *
 *
 * The filter is applied to a series of consecutive time events specified by the C type
 * array @param.
 *
 *
 * The filter piecewise determines if the number of photons within a time window (tw)
 * exceeds @param n_ph_max. If the within a tw the number of photons is smaller than
 * @param n_ph_max, the time events within the tw are added to the selection.
 *
 *
 * @param selection
 * @param n_selected
 * @param time
 * @param n_time
 * @param tw
 * @param n_ph_max
 */
void selection_by_count_rate(
        long long **out, int *n_out,
        unsigned long long *time, int n_time,
        unsigned long tw, int n_ph_max
);


/*!
 * Splits the time trace into bins that are at least of the length specified by @param time_window and
 * counts the number of photons in each time interval
 *
 * @param out array of counts
 * @param n_out number of elements in @param out
 * @param time array of detection times
 * @param n_time number of elements in the @param time array
 * @param time_window The size of the
 */
void histogram_trace(
        int **out, int *n_out,
        unsigned long long *time, int n_time,
        int time_window);


/*!
 *
 *
 *
 * @param ranges
 * @param n_range
 * @param time
 * @param n_time
 * @param channel
 */
void get_ranges_channel(
        int **ranges, int *n_range,
        short *channel, int n_channel,
        int selection_channel
);


/*!
 *
 * @param out
 * @param n_out
 * @param in
 * @param n_in
 * @param routing_channels
 * @param n_routing_channels
 */
void selection_by_channels(
        long long **out, int *n_out,
        long long *in, int n_in,
        short *routing_channels, int n_routing_channels);



template <typename T>
inline void get_array(
        size_t n_valid_events,
        T *array,
        T **out,
        int *n_out
        ){
    try{
        *n_out = (int) n_valid_events;
        (*out) = (T*) malloc(sizeof(T) * n_valid_events);
        for(size_t i=0; i<n_valid_events; i++) (*out)[i] = array[i];
    }
    catch (std::bad_typeid& bt){
        std::cerr << "bad_typeid caught: " << bt.what() << '\n';
    }
}


/*!
 * Determines time windows (tw) for an array of consecutive time events based
 * on a minimum tw size @param tw_min, a minimum number of photons in
 * a tw @param n_ph_min.
 *
 * Optionally, the tw bigger than @param tw_max and tw with more photons than
 * @param n_ph_max are disregarded.
 *
 * The function determines for an array of consecutive time events passed to the
 * function by the argument @param time an interleaved array @param time_windows
 * containing a list of time windows (tw). In the array @param time_windows the
 * beginning and the end of the tws are interleaved, e.g., for two time windows
 *
 *      [begin1, end1, begin2, end2]
 *
 * The returned beginnings and ends refer to the index of the photons in the
 * array of consecutive times @param time.
 *
 *
 * The selection of the tws can be adjusted by the parameters @param tw_min, @param tw_max,
 * @param n_ph_min, and @param n_ph_max.
 *
 *
 * The parameters @param tw_min and @param tw_max determine the minimum and the
 * maximum size of a tw.
 *
 *
 * The parameters @param n_ph_max and @param n_ph_min limit the number of photons
 * within a time window, i.e.,
 *
 *
 * @param[out] ranges A pointer to a C type array that will contain the begin and end of tws.
 * @param[out] n_tw A pointer to an integer that will contain the length of @param ranges.
 * @param[in] time A C type array that that contains the time events.
 * @param[in] n_time The number of time events of @param time.
 * @param[in] tw_min The minimum length of a tw (mandatory).
 * @param[in} tw_max The maximum length of a tw (optional, use -1 if not used).
 * @param[in} n_ph_min The minimum number of photons in a tw (options, use -1 if not used).
 * @param[in} n_ph_max The maximum number of photons in a tw (options, use -1 if not used).
 *
 *
 */
void ranges_by_time_window(
        int **ranges, int *n_range,
        unsigned long long *time, int n_time,
        int tw_min, int tw_max,
        int n_ph_min, int n_ph_max
);


class TTTRRange {

protected:
    unsigned int start;
    unsigned int stop;
    unsigned long long start_time;
    unsigned long long stop_time;
    bool tttr_filled;

public:

    TTTRRange() = default;
    ~TTTRRange() = default;

    virtual std::vector<unsigned int> get_tttr_indices();

    std::vector<unsigned int> get_start_stop(){
        std::vector<unsigned int> v = {start, stop};
        return v;
    }

    std::vector<unsigned long long> get_start_stop_time(){
        std::vector<unsigned long long> v = {start_time, stop_time};
        return v;
    }

    unsigned long long get_duration(){
        return get_start_stop_time()[1] - get_start_stop_time()[0];
    }

};




class TTTR {

    friend class CLSMImage;

private:
    /// the input file
    char* filename;

    std::vector<TTTR*> children;

    Header *header;

    uint64_t overflow_counter;

    /// map to translates string container types to int container types
    std::map<std::string, int> container_names = {
            {std::string("PTU"), 0},
            {std::string("HT3"), 1},
            {std::string("SPC-130"), 2},
            {std::string("SPC-600_256"), 3},
            {std::string("SPC-600_4096"), 4},
            {std::string("PHOTON-HDF5"), 5}
    };

    typedef bool (*processRecord_t)(
            uint64_t&,  // input
            uint64_t&,  // overflow counter
            uint64_t&,  // true number of sync pulses
            uint32_t&,  // microtime
            int16_t&,   // channel number (16bit more than enough, negative numbers - potential future special cases
            int16_t&    // the event type: photon, or marker (overflows are treated separately and removed during reading)
    );

    std::map<int, processRecord_t> processRecord_map = {
            {PQ_RECORD_TYPE_HHT2v1,      ProcessHHT2v1},
            {PQ_RECORD_TYPE_HHT2v2,      ProcessHHT2v2},
            {PQ_RECORD_TYPE_HHT3v1,      ProcessHHT3v1},
            {PQ_RECORD_TYPE_HHT3v2,      ProcessHHT3v2},
            {PQ_RECORD_TYPE_PHT2,        ProcessPHT2},
            {PQ_RECORD_TYPE_PHT3,        ProcessPHT3},
            {BH_RECORD_TYPE_SPC600_256,  ProcessSPC600_256},
            {BH_RECORD_TYPE_SPC600_4096, ProcessSPC600_4096},
            {BH_RECORD_TYPE_SPC130,      ProcessSPC130}
    };

    /*!
     * The type of the TTTR file.
     *
     * Currently the following file types are implemented:
     *
     *  * PQ_PTU_CONTAINER          0
     *  * PQ_HT3_CONTAINER          1
     *  * BH_SPC130_CONTAINER       2
     *  * BH_SPC600_256_CONTAINER   3
     *  * BH_SPC600_4096_CONTAINER  4
     *  * PHOTON_HDF5_CONTAINER     5
     *
     * The numbers correspond to the numbers that should be used when
     * initializing the class.
    */
    int tttr_container_type; // e.g. Becker&Hickl (BH) SPC, PicoQuant (PQ) HT3, PQ-PTU
    int tttr_record_type; // e.g. BH spc132, PQ HydraHarp (HH) T3, PQ HH T2, etc.

    /// The size in bytes per TTTR record
    size_t bytes_per_record;

    /// The input file, i.e., the TTTR file, and the output file for the header
    std::FILE *fp;
    hid_t      hdf5_file;                        /*HDF5 file handle */

    /// marks the end of the header in the input file (to seek the beginning of the tttr records)
    size_t fp_records_begin;

    /// the data contained in the current TTTRRecord
    uint64_t TTTRRecord;

    /*!
    * The reading routine for a photon accepts as a first argumnet a pointer to a 64bit integer.
    * The integer is processed by the reading routing and writes to the
    * @return The return value is true if the record is not an overflow record.
    */

    bool (*processRecord)(
            uint64_t&, // input
            uint64_t&, // overflow counter
            uint64_t&, // true number of sync pulses
            uint32_t&, // microtime
            int16_t&,   // channel number (16bit more than enough, negative numbers - potential future special cases
            int16_t&   // the event type: photon, or marker (overflows are treated separately and removed during reading)
    );

    /// The number of sync pulses
    unsigned long long *macro_times;
    /// Micro time
    unsigned int *micro_times;
    /// The channel number
    short *routing_channels;
    /// The event type
    short *event_types;

    int read_hdf_file(char *fn);

protected:

    /// allocates memory for the records. @param n_rec are the number of records.
    void allocate_memory_for_records(size_t n_rec);

    /// deallocate memory of records
    void deallocate_memory_of_records();

    /// Reads n_records records of the file (n_records is the number of records)
    /// @param n_rec is the number of records that are being read. If no number
    /// of records to be read is specified all records in the file are being read.
    /// If the parameter @param rewind is true (default behaviour) the file is read
    /// from the beginning of the records till the end of the file or till n_red records have been read.
    /// If @param rewind is false the records are being read from the current location of the file
    /// pointer till the end of the file.
    void read_records(size_t n_rec, bool rewind, size_t chunk);
    void read_records(size_t n_rec);
    void read_records();

    /// the number of time tagged data records in the TTTR file
    size_t n_records_in_file;

    /// the number of read time tagged data
    size_t n_records_read;

    /// the number of valid read records (excluded overflow and invalid records)
    size_t n_valid_events;

public:

    void get_macro_time(unsigned long long **out, int *n_out);
    void get_micro_time(unsigned int **out, int *n_out);
    void get_routing_channel(short ** out, int* n_out);
    void get_event_type(short ** out, int* n_out);
    int get_number_of_tac_channels();
    int get_n_valid_events();
    TTTR* select(long long *selection, int n_selection);

    /*! Constructor
     * @param filename is the filename of the TTTR file. @param container_type specifies the file type.
     *        parent->children.push_back()


     * PQ_PTU_CONTAINER          0
     * PQ_HT3_CONTAINER          1
     * BH_SPC130_CONTAINER       2
     * BH_SPC600_256_CONTAINER   3
     * BH_SPC600_4096_CONTAINER  4
     */
    TTTR();

    // Constructors are to read files
    TTTR(char *filename, int container_type, bool read_input);
    TTTR(char *filename, int container_type);
    TTTR(char *filename, const char* container_type);


    // Constructors for in memory data
    TTTR(unsigned long long *n_sync_pulses,
         unsigned int *micro_times,
         short *routing_channels,
         short *event_types
    );

    TTTR(TTTR *parent,
         long long *selection,
         int n_selection
    );


    /// Destructor
    ~TTTR();

    /*!getFilename
     * Getter for the filename of the TTTR file
     *
     * @return The filename of the TTTR file
     */
    char* get_filename();

    /*!
     * Returns a vector containing indices of records that
     * @param in a pointer to an array of int16_tchannel numbers that are used to select indices of photons
     * @param n_in the length of the channel list.
     */
    void get_selection_by_channel(
            long long **out, int *n_out,
            long long *in, int n_in
            );

    /*!
     *
     * @param out
     * @param n_out
     * @param tw
     * @param n_ph_max
     */
    void get_selection_by_count_rate(
            long long **out, int *n_out,
            unsigned long tw, int n_ph_max
            );

    void get_ranges_by_count_rate(
            int **out, int *n_out,
            int tw_min, int tw_max,
            int n_ph_min, int n_ph_max
            );

    /// Get header returns the header (if present) as a map of strings.
    Header get_header();

    /*!
     * Returns the number of events in the TTTR file for cases no selection is specified
     * otherwise the number of selected events is returned.
     * @return
     */
    int get_n_events();

    /*!
     * Reads the TTTR data contained in a file into the TTTR object
     *
     *
     * @param fn The filename
     * @param container_type The container type.
     * @return Returns 1 in case the file was read without errors. Otherwise 0 is returned.
     */
    int read_file(char *fn, int container_type);
    int read_file();

    bool write_file(char *fn, int container_type);
};


#endif //TTTRLIB_TTTR_H
