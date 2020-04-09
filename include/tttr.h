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


#include <cstdint>
#include <string>
#include <cmath>
#include <algorithm>
#include <iostream>
#include <vector>
#include <cstdio>
#include <map>
#include <array>
#include <memory>
#include <stdlib.h>     /* calloc, exit, free */

#include <boost/filesystem.hpp>
#include <boost/bimap.hpp>

#include "hdf5.h"

#include <record_reader.h>
#include <header.h>


#define RECORD_PHOTON               0
#define RECORD_MARKER               1
#define VERSION                     "0.0.14"


/*!
 * Determines the number of records in a TTTR files (not for use with HDF5)
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
 * @param selection output array
 * @param n_selected number of elements in output array
 * @param time array of times
 * @param n_time number of times
 * @param time_window length of the time window
 * @param n_ph_max maximum number of photons in a time window
 * @param macro_time_calibration
 * @param invert if invert is true (default false) only indices where the number
 * of photons exceeds n_ph_max are selected
 */
void selection_by_count_rate(
        unsigned long long **output, int *n_output,
        unsigned long long *time, int n_time,
        double time_window, int n_ph_max,
        double macro_time_calibration=1.0,
        bool invert=false
);


/*!
 * Splits the time trace into bins that are at least of the length specified by @param time_window and
 * counts the number of photons in each time interval
 *
 * @param output array of counts
 * @param n_output number of elements in @param output
 * @param input array of detection times
 * @param n_input number of elements in the @param input array
 * @param time_window The size of the
 */
void histogram_trace(
        int **output, int *n_output,
        unsigned long long *input, int n_input,
        int time_window
        );


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
 * @param output
 * @param n_output
 * @param input
 * @param n_input
 * @param routing_channels
 * @param n_routing_channels
 */
void selection_by_channels(
        unsigned long long **output, int *n_output,
        unsigned long long *input, int n_input,
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
        unsigned long long  **ranges, int *n_range,
        unsigned long long *time, int n_time,
        int tw_min, int tw_max,
        int n_ph_min, int n_ph_max
);


class TTTRRange {

protected:
    size_t start;
    size_t stop;
    unsigned long long start_time;
    unsigned long long stop_time;

public:

    TTTRRange():
    start(0),
    stop(0),
    start_time(0),
    stop_time(0)
    {};

    virtual std::vector<unsigned int > get_tttr_indices(){
        std::vector<unsigned int > v;
        for(size_t i=start; i<stop; i++){
            v.emplace_back(i);
        }
        return v;
    }

    std::vector<unsigned long long> get_start_stop(){
        std::vector<unsigned long long> v = {start, stop};
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

    /*!
     * Copy the information from another TTTR object
     *
     * @param p2 the TTTR object which which the information is copied from
     * @param include_big_data if this is true also the macro time, micro time
     * etc. are copied. Otherwise all other is copied
     */
    void copy_from(const TTTR &p2, bool include_big_data=true);

    /// the input file
    std::string filename;

    std::vector<TTTR*> children;

    Header *header = nullptr;

    uint64_t overflow_counter;

    /// map to translates string container types to int container types
    boost::bimap<std::string, int> container_names;

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
    std::string tttr_container_type_str; // e.g. Becker&Hickl (BH) SPC, PicoQuant (PQ) HT3, PQ-PTU
    int tttr_record_type; // e.g. BH spc132, PQ HydraHarp (HH) T3, PQ HH T2, etc.

    /// The size in bytes per TTTR record
    size_t bytes_per_record;

    /// The input file, i.e., the TTTR file, and the output file for the header
    std::FILE *fp;
    hid_t      hdf5_file;                        /*HDF5 file handle */

    /// marks the end of the header in the input file (to seek the
    /// beginning of the tttr records)
    size_t fp_records_begin;

    /// the data contained in the current TTTRRecord
    uint64_t TTTRRecord;

    /*!
    * The reading routine for a photon accepts as a first argument a
    * pointer to a 64bit integer.
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

    int read_hdf_file(const char *fn);


protected:

    /*!
    * Traverses the routing channel array and lists the used routing channel
    * numbers in the protected attribute used_routing_channels.
    */
    void find_used_routing_channels();

    /// a vector containing the used routing channel numbers in the TTTR file
    std::vector<short> used_routing_channels;

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
    size_t n_records_in_file = 0;

    /// the number of read time tagged data
    size_t n_records_read = 0;

    /// the number of valid read records (excluded overflow and invalid records)
    size_t n_valid_events = 0;

    /*!
     * Reads the TTTR data contained in a file into the TTTR object
     *
     *
     * @param fn The filename
     * @param container_type The container type.
     * @return Returns 1 in case the file was read without errors. Otherwise 0 is returned.
     */
    int read_file(const char *fn, int container_type);
    int read_file();


public:

    /*!
     * Returns an array containing the routing channel numbers
     * that are contained (used) in the TTTR file.
     *
     * @param output Pointer to the output array
     * @param n_output Pointer to the number of elements in the output array
     */
    void get_used_routing_channels(short **output, int *n_output);

    /*!
     * Returns an array containing the macro times of the valid TTTR
     * events.
     *
     * @param output Pointer to the output array
     * @param n_output Pointer to the number of elements in the output array
     */
    void get_macro_time(unsigned long long **output, int *n_output);

    /*!
     * Returns an array containing the micro times of the valid TTTR
     * events.
     *
     * @param output Pointer to the output array
     * @param n_output Pointer to the number of elements in the output array
     */
    void get_micro_time(unsigned int **output, int *n_output);

    /*!
     * Returns an array containing the routing channel numbers of the
     * valid TTTR events.
     *
     * @param output Pointer to the output array
     * @param n_output Pointer to the number of elements in the output array
     */
    void get_routing_channel(short ** output, int* n_output);

    /*!
     *
     * @param output Pointer to the output array
     * @param n_output Pointer to the number of elements in the output array
     */
    void get_event_type(short ** output, int* n_output);

    /*!
     * Returns the number of micro time channels that fit between two
     * macro time clocks.
     *
     * @return maximum valid number of micro time channels
     */
    unsigned int get_number_of_micro_time_channels();

    /*!
     *
     * @return number of valid events in the TTTR file
     */
    unsigned int get_n_valid_events();

    /*!
     *
     * @return the container type that was used to open the file
     */
    std::string get_tttr_container_type(){
        return tttr_container_type_str;
    }

    TTTR* select(unsigned long long *selection, int n_selection);

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

    /// Copy constructor
    TTTR(const TTTR &p2);


    /*!
     *
     * @param filename TTTR filename
     * @param container_type container type as int (0 = PTU; 1 = HT3;
     * 2 = SPC-130; 3 = SPC-600_256; 4 = SPC-600_4096; 5 = PHOTON-HDF5)
     * @param read_input if true reads the content of the file
     */
    TTTR(const char *filename, int container_type, bool read_input);

    /*!
     *
     * @param filename TTTR filename
     * @param container_type container type as int (0 = PTU; 1 = HT3;
     * 2 = SPC-130; 3 = SPC-600_256; 4 = SPC-600_4096; 5 = PHOTON-HDF5)
     */
    TTTR(const char *filename, int container_type);

    /*!
     *
     * @param filename TTTR filename
     * @param container_type container type as string (PTU; HT3;
     * SPC-130; SPC-600_256; SPC-600_4096; PHOTON-HDF5)
     */
    TTTR(const char *filename, const char* container_type);


    /*!
     * Constructor of TTTR object using arrays of the TTTR events
     *
     * If arrays of different size are used to initialize a TTTR object
     * the shortest array of all provided arrays is used to construct the
     * TTTR object.
     *
     * @param macro_times input array containing the macro times
     * @param n_macrotimes  number of macro times
     * @param micro_times input array containing the microtimes
     * @param n_microtimes length of the of micro time array
     * @param routing_channels routing channel array
     * @param n_routing_channels length of the routing channel array
     * @param event_types array of event types
     * @param n_event_types number of elements in the event type array
     */
    TTTR(unsigned long long *macro_times, int n_macrotimes,
         unsigned int *micro_times, int n_microtimes,
         short *routing_channels, int n_routing_channels,
         short *event_types, int n_event_types
    );

    /*!
     * This constructor can be used to create a new TTTR object that only
     * contains records that are specified in the selection array.
     *
     * The selection array is an array of indices. The events with indices
     * in the selection array are copied in the order of the selection array
     * to a new TTTR object.
     *
     * @param parent
     * @param selection
     * @param n_selection
     *
     */
    TTTR(
            const TTTR &parent,
            unsigned long long *selection,
            int n_selection
            );

    /// Destructor
    ~TTTR();

    /*!getFilename
     * Getter for the filename of the TTTR file
     *
     * @return The filename of the TTTR file
     */
    std::string get_filename();

    /*!
     * Returns a vector containing indices of records that
     * @param input a pointer to an array of int16_tchannel numbers that are
     * used to select indices of photons
     * @param n_input the length of the channel list.
     */
    void get_selection_by_channel(
            unsigned long long **output, int *n_output,
            unsigned long long *input, int n_input
            );

    /*!
     * List of indices where the count rate is smaller than a maximum count
     * rate
     *
     * The count rate is specified by providing a time window that slides over
     * the time array and the maximum number of photons within the time window.
     *
     * @param output the output array that will contain the selected indices
     * @param n_output the number of elements in the output array
     * @param time_window the length of the time window
     * @param n_ph_max the maximum number of photons within a time window
     */
    void get_selection_by_count_rate(
            unsigned  long long **output, int *n_output,
            double time_window, int n_ph_max,
            bool invert=false
            );

    void get_ranges_by_count_rate(
            unsigned long long **output, int *n_output,
            int tw_min, int tw_max,
            int n_ph_min, int n_ph_max
            );

    /// Get header returns the header (if present) as a map of strings.
    Header get_header();

    /*!
     * Returns the number of events in the TTTR file for cases no selection
     * is specified otherwise the number of selected events is returned.
     * @return
     */
    unsigned int get_n_events();

    /*!
     * Write the contents of a opened TTTR file to a new
     * TTTR file.
     *
     * @param fn filename
     * @param container_type container type (PTU; HT3;
     * SPC-130; SPC-600_256; SPC-600_4096; PHOTON-HDF5)
     * @return
     */
    bool write(
            const char *fn,
            const char* container_type
            );

    /*!
     * Shift the macro time by a constant
     * @param shift
     */
    void shift_macro_time(int shift);
};


#endif //TTTRLIB_TTTR_H
