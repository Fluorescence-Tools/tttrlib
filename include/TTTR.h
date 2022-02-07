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
#include <memory>       /* shared_ptr */
#include <stdlib.h>     /* malloc, calloc, realloc, exit, free */
#include <numeric>
#include <cinttypes>    /* uint64, int64, etc */

#include "omp.h"
#include <boost/bimap.hpp>
#include <boost/filesystem.hpp> // std::filesystem is not in osx 10.14

#include "hdf5.h"

#include "Histogram.h"
#include "TTTRHeader.h"
#include "TTTRRecordReader.h"
#include "TTTRRecordTypes.h"
#include "info.h"


/*!
 *
 * @brief A count rate (cr) filter that returns an array containing a list of indices where
 * the cr was smaller than a specified cr.
 *
 *
 * @details The filter is applied to a series of consecutive time events. The time events
 * are sliced into time windows tw) which have at least a duration as specified by
 * time_window. The tttr indices of the time windows are written to the
 * output parameter output. Moreover, for every tw the number of
 * photons is determined. If in a tw the number of photons exceeds n_ph_max
 * and invert is false (default) the tw is not written to output.
 * If If in a tw the number of photons is less then n_ph_max and invert
 * is true the tw is not written to output.
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
        int **output, int *n_output,
        unsigned long long *time, int n_time,
        double time_window, int n_ph_max,
        double macro_time_calibration=1.0,
        bool invert=false
);


/*!
* @brief Returns time windows (tw), i.e., the start and the stop indices for a
* minimum tw size, a minimum number of photons in a tw.
*
* @param output [out] Array containing the interleaved start and stop indices
* of the tws in the TTTR object.
* @param n_output [out] Length of the output array
* @param input [in] Array containing the macro times
* @param n_input [in] Number of macro times
* @param minimum_window_length [in] Minimum length of a tw (mandatory).
* @param maximum_window_length [in] Maximum length of a tw (optional).
* @param minimum_number_of_photons_in_time_window [in] Minimum number of
* photons a selected tw contains (optional)
* @param maximum_number_of_photons_in_time_window [in] Maximum number of
* photons a selected tw contains (optional)
* @param invert [in] If set to true, the selection criteria are inverted.
*/
void ranges_by_time_window(
        int **output, int *n_output,
        unsigned long long *input, int n_input,
        double minimum_window_length,
        double maximum_window_length=-1,
        int minimum_number_of_photons_in_time_window=-1,
        int maximum_number_of_photons_in_time_window=-1,
        double macro_time_calibration=1.0,
        bool invert=false
);


/*!
 * Computes a intensity trace for a sequence of time events
 *
 * The intensity trace is computed by splitting the trace of time events into
 * time windows (tws) with a minimum specified length and counts the number
 * of photons in each tw.
 *
 * @param output number of photons in each time window
 * @param n_output number of time windows
 * @param input array of time points
 * @param n_input number number of time points
 * @param time_window time window size in units of the macro time resolution
 * @param macro_time_resolution the resolution of the macro time clock
 */
void compute_intensity_trace(
        int **output, int *n_output,
        unsigned long long *input, int n_input,
        double time_window_length,
        double macro_time_resolution = 1.0
);



// Seems unused
// * Get the ranges in for a specific channel number
// *
// * @param[out] ranges
// * @param[out] n_range
// * @param[in] channel
// * @param[in] n_channel
// * @param[in] channel
// */
//void get_ranges_channel(
//        unsigned int **ranges, int *n_range,
//        short *channel, int n_channel,
//        int selection_channel
//);
//

/*!
 * Selects a subset of indices by a  list of routing channel numbers.
 *
 * The retuned set of indices will have routing channel numbers that are in
 * the list of the provided routing channel numbers.
 *
 * @param output[out] output array that will contain the selected indices
 * @param n_output[out] the length of the output array
 * @param input[int] routing channel numbers defining the returned subset of indices
 * @param n_input[int] the length of the input array
 * @param routing_channels[int] array of routing channel numbers. A subset of this
 * array will be selected by the input.
 * @param n_routing_channels[int] the length of the routing channel number array.
 */
void selection_by_channels(
        int **output, int *n_output,
        int *input, int n_input,
        signed char *routing_channels, int n_routing_channels
);


template <typename T>
inline void get_array(size_t n_valid_events, T *array, T **out, int *n_out){
    *n_out = (int) n_valid_events;
    (*out) = (T*) malloc(sizeof(T) * n_valid_events);
    for(size_t i=0; i<n_valid_events; i++) (*out)[i] = array[i];
}


class TTTR : public std::enable_shared_from_this<TTTR>{

    friend class CLSMImage;
    friend class TTTRRange;
    friend class CorrelatorPhotonStream;

private:

    /// the input file
    std::string filename;

    TTTRHeader *header = nullptr;

    /// Global overflow counter that counts the total number of overflows in a TTTR file
    uint64_t overflow_counter;

    /// map to translates string container types to int container types
    boost::bimap<std::string, int> container_names;

    typedef bool (*processRecord_t)(
            uint32_t&,  // input
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

    /// A string that defines the TTTR container type
    std::string tttr_container_type_str; // e.g. Becker&Hickl (BH) SPC, PicoQuant (PQ) HT3, PQ-PTU
    int tttr_record_type; // e.g. BH spc132, PQ HydraHarp (HH) T3, PQ HH T2, etc.

    /// The input file, i.e., the TTTR file, and the output file for the header
    std::FILE *fp;                          /* File handle for all other file types */
    hid_t hdf5_file;                        /*HDF5 file handle */

    /// End the end of the header the begining of the tttr records in the input file
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
            uint32_t&, // input
            uint64_t&, // overflow counter
            uint64_t&, // true number of sync pulses
            uint32_t&, // microtime
            int16_t&,   // channel number (16bit more than enough, negative numbers - potential future special cases
            int16_t&   // the event type: photon, or marker (overflows are treated separately and removed during reading)
    );

    /// The number of sync pulses
    unsigned long long *macro_times;

    /// Micro time
    unsigned short *micro_times;

    /// The channel number
    signed char *routing_channels;

    /// The event type
    signed char *event_types;

    /// the number of time tagged data records in the TTTR file
    size_t n_records_in_file = 0;

    /// the number of read time tagged data
    size_t n_records_read = 0;

    /// the number of valid read records (excluded overflow and invalid records)
    size_t n_valid_events = 0;

    /// allocates memory for the records. @param n_rec are the number of records.
    void allocate_memory_for_records(size_t n_rec);

    /// deallocate memory of records
    void deallocate_memory_of_records();

    /*!
     * Reads the content of a Photon HDF file.
     *
     * WARNING: Only the micro time, the macro time, and the routing channel
     * number are read. The meta data is not proccessed.
     *
     * @param fn filename pointing to the Photon HDF file
     * @return
     */
    int read_hdf_file(const char *fn);

    /// Reads n_records records of the file (n_records is the number of records)
    /// @param n_rec is the number of records that are being read. If no number
    /// of records to be read is specified all records in the file are being read.
    /// If the parameter @param rewind is true (default behaviour) the file is read
    /// from the beginning of the records till the end of the file or till n_red
    /// records have been read. If @param rewind is false the records are being
    /// read from the current location of the file pointer till the end of the file.
    void read_records(size_t n_rec, bool rewind, size_t chunk);
    void read_records(size_t n_rec);
    void read_records();

protected:

    /*!
    * Traverses the routing channel array and lists the used routing channel
    * numbers in the protected attribute used_routing_channels.
    */
    void find_used_routing_channels();

    /// a vector containing the used routing channel numbers in the TTTR file
    std::vector<signed char> used_routing_channels;


public:

    /// Make shared pointer
    std::shared_ptr<TTTR> Get() {return shared_from_this();}

    /*!
    * Copy the information from another TTTR object
    *
    * @param p2 the TTTR object which which the information is copied from
    * @param include_big_data if this is true also the macro time, micro time
    * etc. are copied. Otherwise all other is copied
    */
    void copy_from(const TTTR &p2, bool include_big_data = true);

    /*!
    * Reads the TTTR data contained in a file into the TTTR object
    *
    * @param fn The filename that is read. If fn is a nullptr (default value
    * is nullptr) the filename attribute of the TTTR object is used as
    * filename.
    *
    * @param container_type The container type.
    * @return Returns 1 in case the file was read without errors. Otherwise 0 is returned.
    */
    int read_file(const char *fn = nullptr, int container_type = -1);

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
    static size_t get_number_of_records_by_file_size(
            std::FILE *fp,
            size_t offset,
            size_t bytes_per_record
    );

    void append_events(
            unsigned long long *macro_times, int n_macrotimes,
            unsigned short *micro_times, int n_microtimes,
            signed char *routing_channels, int n_routing_channels,
            signed char *event_types, int n_event_types,
            bool shift_macro_time = true,
            long long macro_time_offset = 0
    );

    void append_event(
            unsigned long long macro_time,
            unsigned short micro_time,
            signed char routing_channel,
            signed char event_type,
            bool shift_macro_time = true,
            long long macro_time_offset = 0
    );

    void append(
            const TTTR *other,
            bool shift_macro_time=true,
            long long macro_time_offset=0
    );

    size_t size(){
        return get_n_valid_events();
    }

    /*!
     * Returns an array containing the routing channel numbers
     * that are contained (used) in the TTTR file.
     *
     * @param output Pointer to the output array
     * @param n_output Pointer to the number of elements in the output array
     */
    void get_used_routing_channels(signed char **output, int *n_output);

    /*!
     * Returns an array containing the macro times of the valid TTTR
     * events.
     *
     * @param output Pointer to the output array
     * @param n_output Pointer to the number of elements in the output array
     */
    void get_macro_times(unsigned long long **output, int *n_output);

    /*!
     * Returns an array containing the micro times of the valid TTTR
     * events.
     *
     * @param output Pointer to the output array
     * @param n_output Pointer to the number of elements in the output array
     */
    void get_micro_times(unsigned short **output, int *n_output);

    /*!
     * Returns a intensity trace that is computed for a specified integration
     * window
     *
     * @param output the returned intensity trace
     * @param n_output the number of points in the intensity trace
     * @param time_window_length the length of the integration time windows in
     * units of milliseconds.
     */
    void get_intensity_trace(int **output, int *n_output, double time_window_length=1.0);

    /*!
     * Returns an array containing the routing channel numbers of the
     * valid TTTR events.
     *
     * @param output Pointer to the output array
     * @param n_output Pointer to the number of elements in the output array
     */
    void get_routing_channel(signed char** output, int* n_output);

    /*!
     *
     * @param output Pointer to the output array
     * @param n_output Pointer to the number of elements in the output array
     */
    void get_event_type(signed char** output, int* n_output);

    /*!
     * Returns the number of micro time channels that fit between two
     * macro time clocks.
     *
     * @return maximum valid number of micro time channels
     */
    unsigned int get_number_of_micro_time_channels();

    /*!
     * @return number of valid events in the TTTR file
     */
    size_t get_n_valid_events();

    /*!
     * @return the container type that was used to open the file
     */
    std::string get_tttr_container_type(){
        return tttr_container_type_str;
    }

    std::shared_ptr<TTTR> select(int *selection, int n_selection);

    /*! Constructor
     */
    TTTR();

    /// Copy constructor
    TTTR(const TTTR &p2);

    /*!
     * Constructor that can read a file
     *
     * @param filename TTTR filename
     * @param container_type container type as int (0 = PTU; 1 = HT3;
     * 2 = SPC-130; 3 = SPC-600_256; 4 = SPC-600_4096; 5 = PHOTON-HDF5)
     * @param read_input if true reads the content of the file
     *
     * PQ_PTU_CONTAINER          0
     * PQ_HT3_CONTAINER          1
     * BH_SPC130_CONTAINER       2
     * BH_SPC600_256_CONTAINER   3
     * BH_SPC600_4096_CONTAINER  4
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
     * @param find_used_channels if set to true (default) searches all indices
     * to find the used routing channels
     */
    TTTR(unsigned long long *macro_times, int n_macrotimes,
         unsigned short *micro_times, int n_microtimes,
         signed char *routing_channels, int n_routing_channels,
         signed char *event_types, int n_event_types,
         bool find_used_channels = true
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
     * @param find_used_channels if set to true (default) searches all indices
     * to find the used routing channels
     *
     */
    TTTR(const TTTR &parent,
            int *selection, int n_selection,
            bool find_used_channels = true);

    /// Destructor
    ~TTTR();

    /*!getFilename
     * Getter for the filename of the TTTR file
     *
     * @return The filename of the TTTR file
     */
    std::string get_filename();

    /*!
     * Get a ptr to a TTTR object that is based on a selection on the current
     * TTTR object. A selection is an array of indices of the TTTR events.
     *
     * @param selection
     * @param n_selection
     * @return
     */
    std::shared_ptr<TTTR> get_tttr_by_selection(int *selection, int n_selection){
        auto p = std::make_shared<TTTR>(*this, selection, n_selection, true);
        return p;
    }

    /*!
    * @brief Returns time windows (tw), i.e., the start and the stop indices for a
    * minimum tw size, a minimum number of photons in a tw.
    *
    * @param output [out] Array containing the interleaved start and stop indices
    * of the tws in the TTTR object.
    * @param n_output [out] Length of the output array
    * @param minimum_window_length [in] Minimum length of a tw (mandatory).
    * @param maximum_window_length [in] Maximum length of a tw (optional).
    * @param minimum_number_of_photons_in_time_window [in] Minimum number of
    * photons a selected tw contains (optional)
    * @param maximum_number_of_photons_in_time_window [in] Maximum number of
    * photons a selected tw contains (optional)
    * @param invert [in] If set to true, the selection criteria are inverted.
    */
    void get_ranges_by_time_window(
            int **output, int *n_output,
            double minimum_window_length,
            double maximum_window_length=-1,
            int minimum_number_of_photons_in_time_window=-1,
            int maximum_number_of_photons_in_time_window=-1,
            double macro_time_calibration = -1,
            bool invert=false
    ){
        if(macro_time_calibration < 0){
            macro_time_calibration = header->get_macro_time_resolution();
        }
        ranges_by_time_window(
                output, n_output,
                macro_times, n_valid_events,
                minimum_window_length,
                maximum_window_length,
                minimum_number_of_photons_in_time_window,
                maximum_number_of_photons_in_time_window,
                macro_time_calibration,
                invert
        );
    }

    /*!
      * Get events indices by the routing channel number
      *
      * This method returns an array that contains the event / photon indices
      * of events with routing channel numbers that are found in the selection
      * input array.
      *
      * @param output indices of the events
      * @param n_output number of selected events
      * @param input routing channel number for selection of events
      * @param n_input number of routing channels for selection of events
      */
    void get_selection_by_channel(
            int **output, int *n_output,
            int *input, int n_input
    );

    std::shared_ptr<TTTR> get_tttr_by_channel(int *input, int n_input){
        int* sel; int nsel;
        get_selection_by_channel(&sel, &nsel, input, n_input);
        return get_tttr_by_selection(sel, nsel);
    }

    /*!
     * List of indices where the count rate is smaller than a maximum count
     * rate
     *
     * The count rate is specified by providing a time window that slides over
     * the time array and the maximum number of photons within the time window.
     *
     * @param output the output array that will contain the selected indices
     * @param n_output the number of elements in the output array
     * @param time_window the length of the time window in milliseconds
     * @param n_ph_max the maximum number of photons within a time window
     */
    void get_selection_by_count_rate(
            int **output, int *n_output,
            double time_window, int n_ph_max,
            bool invert=false
    );

    std::shared_ptr<TTTR> get_tttr_by_count_rate(
            double time_window, int n_ph_max,
            bool invert=false
    ){
        int* sel; int nsel;
        get_selection_by_count_rate(
                &sel, &nsel,
                time_window, n_ph_max, invert);
        return get_tttr_by_selection(sel, nsel);
    }

    /*!
    * Returns time windows (tw), i.e., the start and the stop indices for a
    * minimum tw size, a minimum number of photons in a tw.
    *
    * @param output[out] Array containing the interleaved start and stop indices
    * of the tws in the TTTR object.
    * @param n_output[out] Length of the output array
    * @param minimum_window_length[in] Minimum length of a tw in units of ms (mandatory).
    * @param maximum_window_length[in] Maximum length of a tw (optional).
    * @param minimum_number_of_photons_in_time_window[in] Minimum number of
    * photons a selected tw contains (optional) in units of seconds
    * @param maximum_number_of_photons_in_time_window[in] Maximum number of
    * photons a selected tw contains (optional)
    * @param invert[in] If set to true, the selection criteria are inverted.
    */
    void get_time_window_ranges(
            int **output, int *n_output,
            double minimum_window_length,
            int minimum_number_of_photons_in_time_window,
            int maximum_number_of_photons_in_time_window=-1,
            double maximum_window_length=-1.0,
            double macro_time_calibration=-1,
            bool invert = false
    );

    /// Get header returns the header (if present) as a map of strings.
    TTTRHeader* get_header();

    /*!
     * Returns the number of events in the TTTR file for cases no selection
     * is specified otherwise the number of selected events is returned.
     * @return
     */
    size_t get_n_events();

    /*!
     * Write the contents of a opened TTTR file to a new
     * TTTR file.
     *
     * @param fn filename
     * @param container_type container type (PTU; HT3;
     * SPC-130; SPC-600_256; SPC-600_4096; PHOTON-HDF5)
     * @oaram write_a_header if set to false no header is written - Writing correct
     * headers is not implemented. Therefore, the default value is false.
     * @return
     */
    bool write(std::string filename, TTTRHeader* header = nullptr);

    void write_spc132_events(FILE* fp, TTTR* tttr);

    void write_hht3v2_events(FILE* fp, TTTR* tttr);

    void write_header(std::string &fn, TTTRHeader* header = nullptr);

    /*!
     * Shift the macro time by a constant
     * @param shift
     */
    void shift_macro_time(int shift);

    TTTR* operator+(const TTTR* other) const
    {
        auto re = new TTTR();
        re->copy_from(*this, true);
        re->append(other);
        return re;
    }

    /*!
     * Computes a histogram of the TTTR data's micro times
     *
     * @param tttr_data a pointer to the TTTR data
     * @param histogram pointer to which the histogram will be written (the memory
     * is allocated but the method)
     * @param n_histogram the number of points in the histogram
     * @param time pointer to the time axis of the histogram (the memory is allocated
     * by the method)
     * @param n_time the number of points in the time axis
     * @param micro_time_coarsening a factor by which the micro times in the TTTR
     * object are divided (default value is 1).
     */
    static void compute_microtime_histogram(
            TTTR *tttr_data,
            double** output, int* n_output,
            double **time, int *n_time,
            unsigned short micro_time_coarsening = 1
    );

    void get_microtime_histogram(
            double **histogram, int *n_histogram,
            double **time, int *n_time,
            unsigned short micro_time_coarsening = 1
    ){
        compute_microtime_histogram(
                this, histogram, n_histogram,
                time, n_time,
                micro_time_coarsening
        );
    }

    /*!
     * Compute a mean lifetime by the moments of the decay and the instrument
     * response function.
     *
     * The computed lifetime is the first lifetime determined by the method of
     * moments (Irvin Isenberg, 1973, Biophysical journal).
     *
     * @param tttr_data TTTR object for which the lifetime is computed
     * @param tttr_irf TTTR object that is used as IRF
     * @param m0_irf[in] Number of counts in the IRF (used if no TTTR object for IRF provided.
     * @param m1_irf[in] First moment of the IRF (used if no TTTR object for IRF provided.
     * @param tttr_indices[in] Optional list of indices for selecting a subset of the TTTR
     * @param dt[in] Time resolution of the micro time. If not provided extracted from the header (slow)
     * @param minimum_number_of_photons[in] Minimum number of photons. If less photons are in the dataset
     * returns -1 as computed lifetime
     * @return The computed lifetime
     */
    static double compute_mean_lifetime(
            TTTR *tttr_data,
            TTTR *tttr_irf = nullptr,
            double m0_irf = 1, double m1_irf = 1,
            std::vector<int> *tttr_indices = nullptr,
            double dt = -1.0,
            int minimum_number_of_photons = 1
    );

    /*!
     * Compute the mean lifetime by the moments of the decay and the instrument
     * response function.
     */
    double mean_lifetime(
            TTTR *tttr_irf = nullptr,
            int m0_irf = 1, int m1_irf = 1,
            std::vector<int> *tttr_indices = nullptr,
            double dt = -1.0,
            int minimum_number_of_photons = 1
    ){
        return compute_mean_lifetime(
                this,
                tttr_irf, m0_irf, m1_irf,
                tttr_indices, dt, minimum_number_of_photons
        );
    }

    /*!
     * Compute the count rate
     *
     * @param tttr_data[in] TTTR object for which the lifetime is computed
     * @param macrotime_resolution[in] If negative (default) reads macrotime resolution from header (slow)
     * @return Count rate
     */
    static double compute_count_rate(
            TTTR *tttr_data,
            std::vector<int> *tttr_indices = nullptr,
            double macrotime_resolution = -1.0
    );

    double get_count_rate(
            std::vector<int> *tttr_indices = nullptr,
            double macrotime_resolution = -1.0
    ){
        return compute_count_rate(this, tttr_indices, macrotime_resolution);
    }

    static double compute_mean_microtime(
            TTTR *tttr_data,
            std::vector<int> *tttr_indices = nullptr,
            double microtime_resolution = -1.0,
            int minimum_number_of_photons = 1
    );

    double get_mean_microtime(
            std::vector<int> *tttr_indices = nullptr,
            double microtime_resolution = -1.0,
            int minimum_number_of_photons = 1
    ){
        return compute_mean_microtime(this, tttr_indices, microtime_resolution, minimum_number_of_photons);
    }

};


#endif //TTTRLIB_TTTR_H
