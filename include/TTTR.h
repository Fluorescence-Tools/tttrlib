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
#include <fstream> /* ifstream */

#include <boost/bimap.hpp>
//#include <boost/filesystem.hpp> // std::filesystem is not in osx 10.14

#ifdef BUILD_PHOTON_HDF
#include "hdf5.h"
#endif

#include "Histogram.h"
#include "TTTRHeader.h"
#include "FileCheck.h"
#include "TTTRMask.h"
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
 * @param output output array
 * @param n_output number of elements in output array
 * @param time array of times
 * @param n_time number of times
 * @param time_window length of the time window
 * @param n_ph_max maximum number of photons in a time window
 * @param macro_time_calibration
 * @param invert if invert is true (default false) only indices where the number
 * @param make_mask if true (default false) returns array filled with -1 or idx of
 * length n_time
 * of photons exceeds n_ph_max are selected
 */
void selection_by_count_rate(
        int **output, int *n_output,
        unsigned long long *time, int n_time,
        double time_window, int n_ph_max,
        double macro_time_calibration=1.0,
        bool invert=false, bool make_mask=false
);


/*!
 * \brief Returns time windows (tw), i.e., the start and stop indices for a
 * minimum tw size and a minimum number of photons in a tw.
 *
 * @param output [out] Array containing the interleaved start and stop indices
 * of the time windows in the TTTR object.
 * @param n_output [out] Length of the output array.
 * @param input [in] Array containing the macro times.
 * @param n_input [in] Number of macro times.
 * @param minimum_window_length [in] Minimum length of a time window (mandatory).
 * @param maximum_window_length [in] Maximum length of a time window (optional).
 * @param minimum_number_of_photons_in_time_window [in] Minimum number of
 * photons a selected time window must contain (optional).
 * @param maximum_number_of_photons_in_time_window [in] Maximum number of
 * photons a selected time window can contain (optional).
 * @param macro_time_calibration [in] Calibration factor for macro times (default is 1.0).
 * @param invert [in] If set to true, the selection criteria are inverted.
 */
void ranges_by_time_window(
    int **output, int *n_output,
    unsigned long long *input, int n_input,
    double minimum_window_length,
    double maximum_window_length = -1,
    int minimum_number_of_photons_in_time_window = -1,
    int maximum_number_of_photons_in_time_window = -1,
    double macro_time_calibration = 1.0,
    bool invert = false
);


/*!
 * \brief Computes the intensity trace for a sequence of time events.
 *
 * The intensity trace is calculated by partitioning the time events into
 * time windows with a minimum specified length and counting the number
 * of photons in each window.
 *
 * \param output Pointer to an array storing the number of photons in each time window.
 * \param n_output Pointer to the variable storing the number of time windows.
 * \param input Array of time points representing the time events.
 * \param n_input Number of time points in the input array.
 * \param time_window_length Size of the time window in units of the macro time resolution.
 * \param macro_time_resolution The resolution of the macro time clock (default is 1.0).
 *
 * The function calculates the intensity trace by dividing the time events into
 * non-overlapping time windows of the specified length. The output array holds
 * the count of photons in each time window, and n_output is updated accordingly.
 * The input array contains the time points of events, and n_input is the total
 * number of events. The time_window_length parameter defines the size of the
 * time windows, and macro_time_resolution specifies the resolution of the macro
 * time clock (default is 1.0). The calculated intensity trace is stored in the
 * output array, and the total number of time windows is updated in n_output.
 */
void compute_intensity_trace(
        int **output, int *n_output,
        unsigned long long *input, int n_input,
        double time_window_length,
        double macro_time_resolution = 1.0
);


/*!
 * \brief Extracts a subarray of valid events from the input array.
 *
 * This function takes an array of type T, representing events, and extracts
 * a subarray containing the first `n_valid_events` elements. The size of the
 * output array is updated in `n_out`. Memory is allocated for the output array,
 * and the caller is responsible for freeing this memory when it is no longer needed.
 *
 * \tparam T The data type of the array elements.
 * \param n_valid_events Number of valid events to extract.
 * \param array Pointer to the input array containing events of type T.
 * \param[out] out Pointer to the output array holding the extracted subarray.
 * \param[out] n_out Pointer to the variable storing the size of the output array.
 *
 * The function allocates memory for the output array and copies the first
 * `n_valid_events` elements from the input array. The size of the output array
 * is updated in `n_out`. It is the responsibility of the caller to free the
 * allocated memory using `free(*out)` when the output array is no longer needed.
 */
template <typename T>
inline void get_array(
    size_t n_valid_events,  ///< [in] Number of valid events to extract.
    T *array,               ///< [in] Pointer to the input array containing events of type T.
    T **out,                ///< [out] Pointer to the output array holding the extracted subarray.
    int *n_out              ///< [out] Pointer to the variable storing the size of the output array.
) {
    *n_out = static_cast<int>(n_valid_events);
    *out = static_cast<T*>(malloc(sizeof(T) * n_valid_events));

    for (size_t i = 0; i < n_valid_events; ++i) {
        (*out)[i] = array[i];
    }
}


class TTTRMask;

/*!
 * \class TTTR
 * \brief Time-Tagged Time-Resolved (TTTR) data class.
 *
 * The TTTR class represents Time-Tagged Time-Resolved data, which is commonly
 * used in time-correlated single-photon counting (TCSPC) experiments. It inherits
 * from std::enable_shared_from_this to facilitate shared ownership through
 * std::shared_ptr.
 *
 * The class includes friend declarations for CLSMImage, TTTRRange, TTTRMask,
 * and CorrelatorPhotonStream classes, allowing these classes to access the
 * private and protected members of TTTR.
 *
 * TTTR data typically consists of time-tagged events, and this class provides
 * functionality to work with and analyze such data.
 *
 * \note This class is designed to be used in conjunction with other classes
 * such as CLSMImage, TTTRRange, TTTRMask, and CorrelatorPhotonStream.
 *
 *
 * \see CLSMImage
 * \see TTTRRange
 * \see TTTRMask
 * \see CorrelatorPhotonStream
 */
class TTTR : public std::enable_shared_from_this<TTTR>{

    friend class CLSMImage;
    friend class TTTRRange;
    friend class TTTRMask;
    friend class CorrelatorPhotonStream;

private:

    /// Global overflow counter that counts the total number of overflows in a TTTR file
    uint64_t overflow_counter;

    /// the input file
    std::string filename;

    TTTRHeader *header = nullptr;

    /// map to translates string container types to int container types
    boost::bimap<std::string, int> container_names = {};

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
            {BH_RECORD_TYPE_SPC130,      ProcessSPC130},
            {CZ_RECORD_TYPE_CONFOCOR3,   ProcessCzRaw}
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
     *  * CZ_CONFOCOR3_CONTAINER    6
     *
     * The numbers correspond to the numbers that should be used when
     * initializing the class.
    */
    int tttr_container_type; // e.g. Becker&Hickl (BH) SPC, PicoQuant (PQ) HT3, PQ-PTU

    /// A string that defines the TTTR container type
    std::string tttr_container_type_str; // e.g. Becker&Hickl (BH) SPC, PicoQuant (PQ) HT3, PQ-PTU
    int tttr_record_type;                // e.g. BH spc132, PQ HydraHarp (HH) T3, PQ HH T2, etc.

    /// The input file, i.e., the TTTR file, and the output file for the header
    std::FILE *fp;                          /* File handle for all other file types */

#ifdef BUILD_PHOTON_HDF
    hid_t hdf5_file;                        /*HDF5 file handle */
#endif


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

    /*!
     * \brief Allocates memory for storing time-tagged records.
     *
     * This method allocates memory to store a specified number of time-tagged records.
     * The number of records to be allocated is specified by the parameter `n_rec`.
     *
     * \param n_rec Number of records to allocate memory for.
     *
     * \note Call this method to allocate memory before storing time-tagged records.
     * \warning Ensure to free the allocated memory when it is no longer needed.
     *
     * \see deallocate_memory_for_records
     */
    void allocate_memory_for_records(size_t n_rec);

    /*!
     * \brief Deallocates memory used for storing time-tagged records.
     *
     * This method frees the memory that was previously allocated for storing time-tagged records.
     * Call this method when the memory is no longer needed to prevent memory leaks.
     *
     * \note Ensure to call this method only when the records are no longer in use.
     * \see allocate_memory_for_records
     */
    void deallocate_memory_of_records();

    /*!
     * \brief Reads the essential content from a Photon HDF file.
     *
     * Reads the micro time, macro time, and routing channel number from the specified Photon HDF file.
     * This method only processes the fundamental data, excluding meta data.
     *
     * \param fn Filename pointing to the Photon HDF file.
     * \return Returns an integer indicating the success or failure of the file reading operation.
     */
    int read_hdf_file(const char *fn);

    /*!
     * \brief Reads the essential content from a SM file.
     *
     * Reads the macro time, and routing channel number from the specified Photon SM file.
     *
     * \param fn Filename pointing to the Photon HDF file.
     * \return Returns an integer indicating the success or failure of the file reading operation.
     */
    int read_sm_file(const char *fn);

    /*!
     * \brief Reads a specified number of records from the file.
     *
     * Reads 'n_rec' records from the file. If 'n_rec' is not specified, all records in the file
     * are read. If 'rewind' is true (default behavior), the file is read from the beginning
     * of the records until the end of the file or until 'n_rec' records have been read. If 'rewind'
     * is false, the records are read from the current location of the file pointer until the end of the file.
     *
     * \param n_rec Number of records to read. If not specified, all records in the file are read.
     * \param rewind If true (default), the file is read from the beginning; if false, reads from the current position.
     * \param chunk The size of the chunk to read at a time.
     */
    void read_records(size_t n_rec, bool rewind, size_t chunk);

    /*!
     * \brief Reads a specified number of records from the file.
     *
     * Reads 'n_rec' records from the file. If 'n_rec' is not specified, all records in the file
     * are read.
     *
     * \param n_rec Number of records to read. If not specified, all records in the file are read.
     */
    void read_records(size_t n_rec);

    /*!
     * \brief Reads records from the current file position to the end.
     *
     * Reads records from the current file position to the end of the file.
     * No specific number of records is specified.
     */
    void read_records();

protected:

    /*!
     * \brief Traverses the routing channel array and identifies used routing channel numbers.
     *
     * Traverses the routing channel array and populates the protected attribute
     * used_routing_channels with the routing channel numbers that are in use.
     */
    void find_used_routing_channels();

    /// \brief A routing channel is a numeric identifier associated with each photon
    ///        in the time-tagged time-resolved (TTTR) data. It signifies the path
    ///        or channel through which the photon is detected or routed.
    std::vector<signed char> used_routing_channels;


public:

    /// \brief Returns a shared pointer to the current instance of TTTR.
    ///
    /// This function is used to create a shared pointer to the current instance of
    /// the TTTR class. It allows managing the ownership of the object using
    /// shared pointers.
    ///
    /// \return A shared pointer to the current instance of TTTR.
    std::shared_ptr<TTTR> Get() { return shared_from_this(); }

    /*!
     * \brief Copies information from another TTTR object.
     *
     * This function allows copying information from another TTTR object, including
     * optional components based on the specified parameters.
     *
     * @param p2 The TTTR object from which the information is copied.
     * @param include_big_data If true, macro time, micro time, etc., are also copied.
     *                         Otherwise, only essential information is copied.
     */
    void copy_from(const TTTR &p2, bool include_big_data = true);

    /*!
     * \brief Reads TTTR data from a file into the TTTR object.
     *
     * This function reads TTTR data from the specified file into the TTTR object.
     * If the filename is not provided (default is nullptr), the filename attribute
     * of the TTTR object is used. The container_type parameter specifies the type
     * of container used in the file.
     *
     * @param fn The filename to read. If nullptr (default), the TTTR object's filename is used.
     * @param container_type The container type.
     * @return Returns 1 if the file is read without errors; otherwise, returns 0.
     */
    int read_file(const char *fn = nullptr, int container_type = -1);

    /*!
     * \brief Determines the number of records in a TTTR file (not for use with HDF5).
     *
     * Calculates the number of records in the file based on the file size. If the offset
     * is passed, the number of records is calculated using the file size, offset, and
     * bytes_per_record. If the offset is not specified, the current location of the file
     * pointer is used. If bytes_per_record is not specified, the attribute value
     * bytes_per_record of the class instance is used.
     *
     * @param fp The file pointer to the TTTR file.
     * @param offset The offset for calculating the number of records.
     * @param bytes_per_record The number of bytes per record.
     * @return Returns the calculated number of records.
     */
    static size_t get_number_of_records_by_file_size(
        std::FILE *fp,
        size_t offset,
        size_t bytes_per_record
    );

    /*!
     * \brief Appends events to the TTTR object.
     *
     * Appends events represented by macro_times, micro_times, routing_channels,
     * and event_types to the TTTR object. The sizes of the input arrays
     * (n_macrotimes, n_microtimes, n_routing_channels, n_event_types) must be equal.
     *
     * @param macro_times Array of macro time values.
     * @param n_macrotimes Number of elements in the macro_times array.
     * @param micro_times Array of micro time values.
     * @param n_microtimes Number of elements in the micro_times array.
     * @param routing_channels Array of routing channel values.
     * @param n_routing_channels Number of elements in the routing_channels array.
     * @param event_types Array of event type values.
     * @param n_event_types Number of elements in the event_types array.
     * @param shift_macro_time Flag indicating whether to shift macro times.
     * @param macro_time_offset Offset applied to macro times if shift_macro_time is true.
     */
    void append_events(
        unsigned long long *macro_times, int n_macrotimes,
        unsigned short *micro_times, int n_microtimes,
        signed char *routing_channels, int n_routing_channels,
        signed char *event_types, int n_event_types,
        bool shift_macro_time = true,
        long long macro_time_offset = 0
    );

    /*!
     * \brief Appends a single event to the TTTR object.
     *
     * Appends a single event represented by macro_time, micro_time, routing_channel,
     * and event_type to the TTTR object.
     *
     * @param macro_time Macro time value for the event.
     * @param micro_time Micro time value for the event.
     * @param routing_channel Routing channel value for the event.
     * @param event_type Event type value for the event.
     * @param shift_macro_time Flag indicating whether to shift macro time.
     * @param macro_time_offset Offset applied to macro time if shift_macro_time is true.
     */
    void append_event(
        unsigned long long macro_time,
        unsigned short micro_time,
        signed char routing_channel,
        signed char event_type,
        bool shift_macro_time = true,
        long long macro_time_offset = 0
    );

    /*!
     * \brief Appends events from another TTTR object to the current TTTR object.
     *
     * Appends events from another TTTR object (`other`) to the current TTTR object.
     * Optionally shifts macro times and applies an offset to the macro times.
     *
     * @param other Pointer to the TTTR object containing events to append.
     * @param shift_macro_time Flag indicating whether to shift macro times.
     * @param macro_time_offset Offset applied to macro times if shift_macro_time is true.
     */
    void append(
            const TTTR *other,
            bool shift_macro_time=true,
            long long macro_time_offset=0
    );

     /*!
      * \brief Returns the number of valid events in the TTTR data.
      *
      * This function is a wrapper for the get_n_valid_events() method and returns
      * the total number of valid events in the TTTR data.
      *
      * @return The number of valid events.
      */
     size_t size() {
      return get_n_valid_events();
     }

     /*!
      * \brief Retrieves the used routing channel numbers from the TTTR data.
      *
      * This function populates the provided output array with the routing channel
      * numbers that are used in the TTTR file. The number of elements in the output
      * array is stored in the n_output parameter.
      *
      * @param output Pointer to the output array to be populated.
      * @param n_output Pointer to the number of elements in the output array.
      */
     void get_used_routing_channels(signed char **output, int *n_output);

     /*!
      * \brief Retrieves the macro times of valid TTTR events.
      *
      * This function populates the provided output array with the macro times
      * of the valid TTTR events. The number of elements in the output array
      * is stored in the n_output parameter.
      *
      * @param output Pointer to the output array to be populated.
      * @param n_output Pointer to the number of elements in the output array.
      */
     void get_macro_times(unsigned long long **output, int *n_output);

     /*!
      * \brief Retrieves the micro times of valid TTTR events.
      *
      * This function populates the provided output array with the micro times
      * of the valid TTTR events. The number of elements in the output array
      * is stored in the n_output parameter.
      *
      * @param output Pointer to the output array to be populated.
      * @param n_output Pointer to the number of elements in the output array.
      */
     void get_micro_times(unsigned short **output, int *n_output);

     /*!
      * \brief Computes and returns an intensity trace for a specified integration window.
      *
      * The intensity trace is calculated based on the integration time windows,
      * and the result is stored in the output array. The number of points in the
      * intensity trace is returned through the n_output parameter.
      *
      * @param output Pointer to the array to store the intensity trace.
      * @param n_output Pointer to the number of points in the intensity trace.
      * @param time_window_length The length of the integration time windows in
      *        units of milliseconds.
      */
     void get_intensity_trace(int **output, int *n_output, double time_window_length = 1.0);

     /*!
      * \brief Returns an array containing the routing channel numbers of the valid TTTR events.
      *
      * The routing channel numbers are stored in the output array, and the number of
      * elements in the array is returned through the n_output parameter.
      *
      * @param output Pointer to the array to store the routing channel numbers.
      * @param n_output Pointer to the number of elements in the output array.
      */
     void get_routing_channel(signed char** output, int* n_output);

     /*!
      * \brief Returns an array containing the event types of the valid TTTR events.
      *
      * The event types are stored in the output array, and the number of elements in
      * the array is returned through the n_output parameter.
      *
      * @param output Pointer to the array to store the event types.
      * @param n_output Pointer to the number of elements in the output array.
      */
     void get_event_type(signed char** output, int* n_output);

     /*!
      * \brief Returns the number of micro time channels that fit between two macro time clocks.
      *
      * This function calculates and returns the maximum valid number of micro time channels
      * that fit between two macro time clocks.
      *
      * @return Maximum valid number of micro time channels.
      */
     unsigned int get_number_of_micro_time_channels();

     /*!
      * \brief Returns the number of valid events in the TTTR file.
      *
      * This function retrieves and returns the total number of valid events present in
      * the TTTR (Time-Tagged Time-Resolved) file.
      *
      * @return Number of valid events in the TTTR file.
      */
     size_t get_n_valid_events();

     /*!
      * \brief Returns the container type used to open the TTTR file.
      *
      * This function retrieves and returns the container type that was used to open
      * the TTTR (Time-Tagged Time-Resolved) file.
      *
      * @return Container type used to open the TTTR file.
      */
     std::string get_tttr_container_type(){
        return tttr_container_type_str;
    }

     /*!
      * \brief Creates a new TTTR object by selecting specific events based on the provided indices.
      *
      * This function creates a new TTTR (Time-Tagged Time-Resolved) object by selecting specific events
      * from the current TTTR object based on the provided indices.
      *
      * @param selection Pointer to an array containing the indices of selected events.
      * @param n_selection Number of elements in the selection array.
      * @return Shared pointer to the newly created TTTR object containing selected events.
      */
     std::shared_ptr<TTTR> select(int *selection, int n_selection);

    /*!
     * \brief Default constructor for the TTTR (Time-Tagged Time-Resolved) class.
     *
     * This constructor initializes a TTTR object with default values.
     */
    TTTR();

    /*!
     * \brief Copy constructor for the TTTR (Time-Tagged Time-Resolved) class.
     *
     * This constructor creates a new TTTR object by copying the information from another TTTR object.
     *
     * @param p2 The TTTR object from which the information is copied.
     */
    TTTR(const TTTR &p2);

   /*!
    * Constructor that can read a file.
    *
    * @param filename TTTR filename.
    * @param container_type Container type as int:
    *   - 0: PicoQuant PTU Container (PQ_PTU_CONTAINER)
    *   - 1: PicoQuant HT3 Container (PQ_HT3_CONTAINER)
    *   - 2: Becker & Hickl SPC-130 Container (BH_SPC130_CONTAINER)
    *   - 3: Becker & Hickl SPC-600 with 256 channels Container (BH_SPC600_256_CONTAINER)
    *   - 4: Becker & Hickl SPC-600 with 4096 channels Container (BH_SPC600_4096_CONTAINER)
    *   - 5: Photon-HDF5 Container (PHOTON_HDF5_CONTAINER)
    *   - 6: Carl Zeiss ConfoCor3 (CZ_CONFOCOR3_CONTAINER)
    * @param read_input If true, reads the content of the file.
    */
    TTTR(const char *filename, int container_type, bool read_input);

    /**
     * @brief Constructs a TTTR object based on the provided filename by inferring the file type.
     *
     * This constructor initializes a TTTR object by automatically determining the file type
     * from the given filename. It utilizes the `inferTTTRFileType` function to identify
     * the type of TTTR file and then calls the existing constructor with the inferred container type.
     * The internal state related to the container type is initialized based on the file type inference.
     *
     * The constructor performs the following actions:
     * - Infers the container type from the filename using `inferTTTRFileType`.
     * - Initializes the TTTR object using the existing constructor `TTTR(const char *fn, int container_type, bool some_flag)`.
     * - Sets the container type string based on the inferred container type.
     * - Handles cases where the inferred container type is not supported by setting the container type string to `"Unknown"`
     *   and logging an error message.
     *
     * @param filename The path to the TTTR file to be analyzed. The file type is inferred based on this filename.
     */
    TTTR(const char *filename);

    /*!
     * Constructor for TTTR object that reads the content of the file.
     *
     * @param filename TTTR filename.
     * @param container_type Container type as int:
     *   - 0: PicoQuant PTU Container (PQ_PTU_CONTAINER)
     *   - 1: PicoQuant HT3 Container (PQ_HT3_CONTAINER)
     *   - 2: Becker & Hickl SPC-130 Container (BH_SPC130_CONTAINER)
     *   - 3: Becker & Hickl SPC-600 with 256 channels Container (BH_SPC600_256_CONTAINER)
     *   - 4: Becker & Hickl SPC-600 with 4096 channels Container (BH_SPC600_4096_CONTAINER)
     *   - 5: Photon-HDF5 Container (PHOTON_HDF5_CONTAINER)
     *   - 6: Carl Zeiss ConfoCor3 (CZ_CONFOCOR3_CONTAINER)
     */
    TTTR(const char *filename, int container_type);

    /*!
     * Constructor for TTTR object.
     *
     * @param filename TTTR filename.
     * @param container_type Container type as string:
     *   - "PTU": PicoQuant PTU Container
     *   - "HT3": PicoQuant HT3 Container
     *   - "SPC-130": Becker & Hickl SPC-130 Container
     *   - "SPC-600_256": Becker & Hickl SPC-600 with 256 channels Container
     *   - "SPC-600_4096": Becker & Hickl SPC-600 with 4096 channels Container
     *   - "PHOTON-HDF5": Photon-HDF5 Container
     *   - "CZ_CONFOCOR3_CONTAINER": Carl Zeiss ConfoCor3 Container
     */
    TTTR(const char *filename, const char* container_type);

     /*!
      * Constructor for TTTR object using arrays of TTTR events.
      *
      * If arrays of different sizes are used to initialize a TTTR object,
      * the shortest array among all provided arrays is used to construct the TTTR object.
      *
      * @param macro_times Array containing the macro times.
      * @param n_macrotimes Number of macro times.
      * @param micro_times Array containing the microtimes.
      * @param n_microtimes Length of the micro time array.
      * @param routing_channels Routing channel array.
      * @param n_routing_channels Length of the routing channel array.
      * @param event_types Array of event types.
      * @param n_event_types Number of elements in the event type array.
      * @param find_used_channels If set to true (default), searches all indices to find the used routing channels.
      */
     TTTR(unsigned long long *macro_times, int n_macrotimes,
          unsigned short *micro_times, int n_microtimes,
          signed char *routing_channels, int n_routing_channels,
          signed char *event_types, int n_event_types,
          bool find_used_channels = true
     );

     /*!
      * Constructor for creating a new TTTR object containing records specified in the selection array.
      *
      * The selection array is an array of indices. The events with indices
      * in the selection array are copied in the order of the selection array
      * to a new TTTR object.
      *
      * @param parent Parent TTTR object from which to select records.
      * @param selection Array of indices specifying the selected records.
      * @param n_selection Number of elements in the selection array.
      * @param find_used_channels If set to true (default), searches all indices to find the used routing channels.
      */
     TTTR(const TTTR &parent,
             int *selection, int n_selection,
             bool find_used_channels = true);

    /*!
     * Destructor for TTTR class.
     * Releases any allocated resources and cleans up the TTTR object.
     */
    ~TTTR();

    /*!
     * Getter for the filename of the TTTR file.
     *
     * @return The filename of the TTTR file.
     */
    std::string get_filename();

    /*!
     * Get a pointer to a TTTR object that is based on a selection on the current
     * TTTR object. A selection is an array of indices of the TTTR events.
     *
     * @param selection Array of indices of TTTR events.
     * @param n_selection Number of elements in the selection array.
     * @return A shared pointer to a new TTTR object based on the specified selection.
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
     * @param macro_time_calibration [in] Macro time calibration in units of the
     * macro time resolution. If negative, the macro time resolution from the TTTR
     * header is used.
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
       * @brief Get events indices by the routing channel number
       *
       * This method retrieves an array containing the event/ photon indices
       * of events with routing channel numbers found in the selection input array.
       *
       * @param output [out] Indices of the selected events
       * @param n_output [out] Number of selected events
       * @param input [in] Routing channel numbers for selecting events
       * @param n_input [in] Number of routing channels for selection
       */
     void get_selection_by_channel(
             int **output, int *n_output,
             signed char *input, int n_input
     );

     /*!
       * @brief Get a TTTR object based on a selection by routing channel numbers
       *
       * This method creates and returns a shared pointer to a TTTR object that
       * contains only the events with routing channel numbers specified in the input array.
       *
       * @param input [in] Routing channel numbers for selecting events
       * @param n_input [in] Number of routing channels for selection
       * @return Shared pointer to the new TTTR object based on the specified selection
       */
     std::shared_ptr<TTTR> get_tttr_by_channel(signed char *input, int n_input){
      int* sel; int nsel;
      get_selection_by_channel(&sel, &nsel, input, n_input);
      return get_tttr_by_selection(sel, nsel);
     }

     /*!
      * @brief Get indices where the count rate is below a specified maximum
      *
      * This method returns an array of indices where the count rate, calculated
      * within a sliding time window, is below a specified maximum.
      *
      * @param output [out] Array containing the selected indices
      * @param n_output [out] Number of elements in the output array
      * @param time_window [in] Length of the time window in milliseconds
      * @param n_ph_max [in] Maximum number of photons within a time window
      * @param invert [in] If set to true, the selection criteria are inverted
      * @param make_mask [in] If set to true, the output array will be a boolean mask
      */
     void get_selection_by_count_rate(
             int **output, int *n_output,
             double time_window, int n_ph_max,
             bool invert=false, bool make_mask=false
     );


     /*!
      * @brief Get a TTTR object filtered by count rate criteria
      *
      * This method returns a TTTR object filtered based on count rate criteria.
      *
      * @param time_window [in] Length of the time window in milliseconds
      * @param n_ph_max [in] Maximum number of photons within a time window
      * @param invert [in] If set to true, the count rate criteria are inverted
      * @param make_mask [in] If set to true, the output array will be a boolean mask
      * @return A shared pointer to the filtered TTTR object
      */
     std::shared_ptr<TTTR> get_tttr_by_count_rate(
             double time_window, int n_ph_max,
             bool invert=false, bool make_mask=false
     ){
        int* sel; int nsel;
        get_selection_by_count_rate(
                &sel, &nsel,
                time_window, n_ph_max,
                invert, make_mask);
        return get_tttr_by_selection(sel, nsel);
    }

     /*!
      * @brief Get time windows (tw) based on specified criteria
      *
      * Returns time windows (tw), i.e., the start and stop indices for a minimum tw size,
      * a minimum number of photons in a tw.
      *
      * @param output[out] Array containing the interleaved start and stop indices
      * of the tws in the TTTR object.
      * @param n_output[out] Length of the output array
      * @param minimum_window_length[in] Minimum length of a tw in units of ms (mandatory).
      * @param minimum_number_of_photons_in_time_window[in] Minimum number of
      * photons a selected tw contains (optional) in units of seconds
      * @param maximum_number_of_photons_in_time_window[in] Maximum number of
      * photons a selected tw contains (optional)
      * @param maximum_window_length[in] Maximum length of a tw (optional).
      * @param macro_time_calibration[in] Macro time calibration in units of seconds.
      * If negative, the macro time resolution from the header is used.
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

    /*!
     * @brief Get the header as a map of strings.
     *
     * @return Pointer to the TTTRHeader object representing the header information.
     * If no header is present, returns nullptr.
     */
    TTTRHeader* get_header();

     /*!
      * @brief Set the header for the TTTR object.
      *
      * @param v Pointer to the TTTRHeader object containing the header information.
      */
     void set_header(TTTRHeader* v);

     /*!
      * @brief Returns the number of events in the TTTR file, or the number of selected events if a selection is applied.
      *
      * @return Number of events in the TTTR file or the number of selected events.
      */
     size_t get_n_events();

    /*!
     * @brief Writes the contents of an opened TTTR file to a new TTTR file.
     *
     * @param filename The filename for the new TTTR file.
     * @param header Optional TTTRHeader to be written. If set to nullptr, no header is written (default is nullptr).
     * @return True if the write operation is successful, false otherwise.
     */
    bool write(std::string filename, TTTRHeader* header = nullptr);

    /*!
     * @brief Write events from the TTTR object to a file as SPC-132.
     *
     * @param fp The FILE pointer for the output file.
     * @param tttr The TTTR object containing the events to be written.
     */
    void write_spc132_events(FILE* fp, TTTR* tttr);

    /*!
     * @brief Write events from the TTTR object to a file as HHT3v2.
     *
     * @param fp The FILE pointer for the output file.
     * @param tttr The TTTR object containing the events to be written.
     */
    void write_hht3v2_events(FILE* fp, TTTR* tttr);

     /*!
      * @brief Writes the header information to a TTTR file.
      *
      * @param fn The filename to write the header to.
      * @param header Pointer to the TTTRHeader object containing header information.
      */
     void write_header(std::string &fn, TTTRHeader* header = nullptr);

     /*!
      * @brief Shifts the macro time by adding an integer value to each macro time entry.
      *
      * @param shift The integer value added to each macro time entry.
      */
     void shift_macro_time(int shift);

    /*!
      * @brief Adds the events of another TTTR object to the current TTTR object.
      *
      * @param other Pointer to the TTTR object whose events will be added.
      * @return Pointer to a new TTTR object containing the combined events.
      */
    TTTR* operator+(const TTTR* other) const {
        auto re = new TTTR();
        re->copy_from(*this, true);
        re->append(other);
        return re;
    }

     /*!
      * @brief Computes a histogram of the TTTR data's micro times.
      *
      * @param tttr_data Pointer to the TTTR object containing the data.
      * @param output Pointer to which the histogram will be written (memory is allocated by the method).
      * @param n_output Pointer to the number of points in the histogram.
      * @param time Pointer to the time axis of the histogram (memory is allocated by the method).
      * @param n_time Pointer to the number of points in the time axis.
      * @param micro_time_coarsening A factor by which the micro times in the TTTR object are divided (default value is 1).
      * @param tttr_indices Optional pointer to store the indices of TTTR events used in the histogram.
      */
     static void compute_microtime_histogram(
             TTTR *tttr_data,
             double** output, int* n_output,
             double **time, int *n_time,
             unsigned short micro_time_coarsening = 1,
             std::vector<int> *tttr_indices = nullptr
     );

     /*!
      * @brief Computes and returns a histogram of the TTTR data's micro times.
      *
      * @param histogram Pointer to which the histogram will be written (memory is allocated by the method).
      * @param n_histogram Pointer to the number of points in the histogram.
      * @param time Pointer to the time axis of the histogram (memory is allocated by the method).
      * @param n_time Pointer to the number of points in the time axis.
      * @param micro_time_coarsening A factor by which the micro times in the TTTR object are divided (default value is 1).
      */
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
      * @brief Computes the mean lifetime by the moments of the decay and the instrument response function.
      *
      * The computed lifetime is the first lifetime determined by the method of moments (Irvin Isenberg,
      * 1973, Biophysical journal).
      *
      * @param tttr_data TTTR object for which the lifetime is computed.
      * @param tttr_irf TTTR object that is used as IRF.
      * @param m0_irf Number of counts in the IRF (used if no TTTR object for IRF provided).
      * @param m1_irf First moment of the IRF (used if no TTTR object for IRF provided).
      * @param tttr_indices Optional list of indices for selecting a subset of the TTTR.
      * @param dt Time resolution of the micro time. If not provided, extracted from the header (slow).
      * @param minimum_number_of_photons Minimum number of photons. If fewer photons are in the dataset,
      * returns -1 as computed lifetime.
      * @param background Background pattern.
      * @param m0_bg Sum of background photons (overwritten if the background pattern is not empty).
      * @param m1_bg First moment of the background pattern (overwritten if the background
      * pattern is not empty).
      * @param background_fraction Background fraction (if negative, the background is not scaled).
      * @return The computed lifetime.
      */
     static double compute_mean_lifetime(
             TTTR *tttr_data,
             TTTR *tttr_irf = nullptr,
             double m0_irf = 1, double m1_irf = 0,
             std::vector<int> *tttr_indices = nullptr,
             double dt = -1.0,
             int minimum_number_of_photons = 1,
             std::vector<double> *background = nullptr,
             double m0_bg = 0.0, double m1_bg = 0.0,
             double background_fraction = -1.0
     );

     /*!
      * @brief Computes the mean lifetime by moments of decay and instrument response.
      *
      * @param tttr_irf TTTR object used as IRF.
      * @param m0_irf Counts in the IRF (used if no TTTR object for IRF provided).
      * @param m1_irf First moment of the IRF (used if no TTTR object for IRF provided).
      * @param tttr_indices Optional indices for selecting a subset of the TTTR.
      * @param dt Time resolution of the micro time. If not provided, extracted from the header (slow).
      * @param min_ph Minimum number of photons. If fewer photons are in the dataset, returns -1 as computed lifetime.
      * @return Computed mean lifetime.
      */
     double mean_lifetime(
             TTTR *tttr_irf = nullptr,
             int m0_irf = 1, int m1_irf = 1,
             std::vector<int> *tttr_indices = nullptr,
             double dt = -1.0,
             int min_ph = 1
     ){
      return compute_mean_lifetime(
              this,
              tttr_irf, m0_irf, m1_irf,
              tttr_indices, dt, min_ph
      );
     }

     /*!
      * @brief Computes the count rate.
      *
      * @param tttr_data TTTR object for which the count rate is computed.
      * @param tttr_indices Optional indices for selecting a subset of the TTTR.
      * @param macrotime_resolution If negative (default), reads macrotime resolution from header (slow).
      * @return Count rate.
      */
     static double compute_count_rate(
             TTTR *tttr_data,
             std::vector<int> *tttr_indices = nullptr,
             double macrotime_resolution = -1.0
     );

     /*!
      * @brief Gets the count rate.
      *
      * @param tttr_indices Optional indices for selecting a subset of the TTTR.
      * @param macrotime_resolution If negative (default), reads macrotime resolution from header (slow).
      * @return Count rate.
      */
     double get_count_rate(
             std::vector<int> *tttr_indices = nullptr,
             double macrotime_resolution = -1.0
     ){
      return compute_count_rate(this, tttr_indices, macrotime_resolution);
     }

     /*!
      * @brief Computes the mean microtime.
      *
      * @param tttr_data TTTR object for which the mean microtime is computed.
      * @param tttr_indices Optional indices for selecting a subset of the TTTR.
      * @param microtime_resolution If negative (default), reads microtime resolution from header (slow).
      * @param minimum_number_of_photons Minimum number of photons. If less, returns -1 as computed mean microtime.
      * @return The computed mean microtime.
      */
     static double compute_mean_microtime(
             TTTR *tttr_data,
             std::vector<int> *tttr_indices = nullptr,
             double microtime_resolution = -1.0,
             int minimum_number_of_photons = 1
     );


    /*!
    * @brief Gets the mean microtime.
    *
    * @param tttr_indices Optional indices for selecting a subset of the TTTR.
    * @param microtime_resolution If negative (default), reads microtime resolution from header (slow).
    * @param minimum_number_of_photons Minimum number of photons. If less, returns -1 as computed mean microtime.
    * @return The computed mean microtime.
    */
    double get_mean_microtime(
         std::vector<int> *tttr_indices = nullptr,
         double microtime_resolution = -1.0,
         int minimum_number_of_photons = 1
    ){
        return compute_mean_microtime(
                this, tttr_indices,
                microtime_resolution, minimum_number_of_photons);
    }

};


#endif //TTTRLIB_TTTR_H
