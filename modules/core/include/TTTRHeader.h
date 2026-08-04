// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_READHEADER_H
#define TTTRLIB_READHEADER_H

#include <stdlib.h>     /* malloc, calloc, realloc, exit, free */
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <cmath> /* floor, ceil */
#include <string>
#include <string.h> /* strcmp */
#include <algorithm>
#include <vector>
#include <array>
#include <memory>
#include <numeric>
#include <iostream>
#include <sstream>      // std::stringstream
#include "string_encoding.h"
#include <iomanip> /* std::setfill */
#include <fstream> /* ifstream */
#include <cstring> /* std::memcpy */

#include <any>

#ifdef BUILD_PHOTON_HDF
// Forward declarations only -- HighFive::Group appears in a private
// declaration below; the real headers are included in TTTRHeader.cpp.
#include <highfive/bits/H5_definitions.hpp>
#endif

#include <nlohmann/json_fwd.hpp>

#include "Histogram.h"
#include "TTTRRecordReader.h"
#include "TTTRRecordTypes.h"
#include "TTTRHeaderTypes.h"

// some important Tag Idents (TTagHead.Ident) that we will need to read the most common content of a PTU file
// check the output of this program and consult the tag dictionary if you need more
const std::string TTTRTagRes = "MeasDesc_Resolution";              // Resolution for the Dtime (T3 Only) - in seconds
const std::string TTTRTagGlobRes = "MeasDesc_GlobalResolution";    // Global Resolution of TimeTag(T2) /NSync (T3) - in seconds
const std::string TTTRSyncRate = "SyncRate";                       // SyncRate - in Hz
const std::string TTTRNMicroTimes = "MeasDesc_NumberMicrotimes";   // The number of micro time channels
const std::string TTTRRecordType = "MeasDesc_RecordType";         // Internal record type (see tttrlib record type identifier definitions)
const std::string TTTRContainerType = "MeasDesc_ContainerType";   // Internal container type (see tttrlib record type identifier definitions)
const std::string TTTRTagTTTRRecType = "TTResultFormat_TTTRRecType";
const std::string TTTRTagBits = "TTResultFormat_BitsPerRecord";    // Bits per TTTR record
const std::string TTTRTagNumRecords = "TTResult_NumberOfRecords";  // Number of TTTR records in the file
const std::string FileTagEnd = "Header_End";                       // Always appended as last tag (BLOCKEND)


/**
 * Swaps the endianness of a given value.
 *
 * This function takes a reference to a value of any type `T` and swaps its byte order
 * between little-endian and big-endian formats. It uses a union to access the raw bytes
 * of the value and reverses the byte order using `std::reverse_copy`.
 *
 * @tparam T The type of the value whose endianness is to be swapped. Must be trivially
 *            copyable and have a defined byte size.
 * @param val A reference to the value whose endianness is to be swapped. The value is
 *            modified in-place.
 *
 * Example:
 *
 * int32_t original = 0x12345678;
 * SwapEndian(original);
 * // original now contains 0x78563412
 */
template <typename T>
void SwapEndian(T &val) {
    union U {
        T val;
        std::array<std::uint8_t, sizeof(T)> raw;
    } src, dst;

    src.val = val;
    std::reverse_copy(src.raw.begin(), src.raw.end(), dst.raw.begin());
    val = dst.val;
}


class TTTRHeader {

    friend class TTTR;

private:
#ifdef BUILD_PHOTON_HDF
    void process_hdf5_group_datasets(const HighFive::Group& group, const std::string group_name);
#endif

protected:

    /*!
     * JSON object used to store all the header information.
     *
     * Held behind a pointer so this header needs only <nlohmann/json_fwd.hpp>.
     * The full json.hpp costs ~41k preprocessed lines, and TTTRHeader.h is
     * reached from nearly every translation unit through TTTR.h. Never null.
     */
    std::unique_ptr<nlohmann::json> json_data_;

    /// The header metadata. Protected, as the raw member always was.
    nlohmann::json& json_data();
    const nlohmann::json& json_data() const;

    /*!
     * Marks the end of the header in the file (position in file)
     */
    size_t header_end = 0;

public:

    /*!
     * @return The TTTR container type of the associated TTTR file as a char
     */
    int get_tttr_record_type();

    /*!
     *
     * @param v record type
     */
    void set_tttr_record_type(int v);

    /*!
     * The container type
     * @return
     */
    int get_tttr_container_type();

    /*!
     *
     * @param v container type
     */
    void set_tttr_container_type(int v);

    /*!
     * Get a tag / entry from the meta data list in a JSON dict
     *
     * @param json_data
     * @param name
     * @param idx
     * @return
     */
    static nlohmann::json get_tag(
            const nlohmann::json &json_data,
            const std::string &name,
            int idx = -1
    );

    /*!
     * Find the index of a tag in the JSON data by name type and index
     * @param json_data
     * @param name
     * @param type
     * @param idx
     * @return
     */
    static int find_tag(
        nlohmann::json &json_data,
        const std::string &name,
        int idx = -1
    );

    /*!
     * Add a meta data tag. If the tag already exists the value of the meta data
     * tag is replaced.
     *
     * @param json_data
     * @param name
     * @param value
     * @param type
     * @param idx
     */
    static void add_tag(
            nlohmann::json &json_data,
            const std::string &name,
            std::any value,
            unsigned int type = tyAnsiString,
            int idx = -1
    );

    /*!
     * Stores the bytes per TTTR record of the associated TTTR file
     * This attribute is changed when a header is read
    */
    size_t get_bytes_per_record();

    size_t end() const{
        return header_end;
    }

    /*!
     * Number of meta data entries
     */
    size_t size();

    nlohmann::json& operator[](std::size_t idx);

    const nlohmann::json& operator[](std::size_t idx) const;

    /*!
     * The total (possible) number of micro time channels.
     *
     * The number of TAC channels (TAC - Time to analog converter) refers to
     * the number of micro time channels.
     */
     unsigned int get_number_of_micro_time_channels();

    /// Resolution for the macro time in nanoseconds
    double get_macro_time_resolution();

    /// Resolution for the micro time in nanoseconds
    double get_micro_time_resolution();

    /// Set the microtime resolution in nanoseconds
    void set_micro_time_resolution(double resolution);

    /// Set the macro (global) time resolution in seconds
    void set_macro_time_resolution(double resolution);

    /// Set the total number of micro time channels
    void set_number_of_micro_time_channels(int n_channels);

    /// Set an arbitrary floating-point metadata tag by name
    void set_float_tag(const std::string& name, double value);

    /// Set an arbitrary integer metadata tag by name
    void set_int_tag(const std::string& name, int value);

    /// Set an arbitrary binary-blob metadata tag by name (e.g. the HT3 ImgHdr
    /// scan/marker configuration vector)
    void set_blob_tag(const std::string& name, const std::vector<int32_t>& value);

    /// Set an arbitrary ANSI-string metadata tag by name
    void set_string_tag(const std::string& name, const std::string& value);

    /// Duration of a pixel in LSM in units of macro time clock
    int get_pixel_duration();

    /// Duration of a line in LSM in units of macro time clock
    int get_line_duration();

    /*!
     * The number of micro time channels that fit between two macro times.
     *
     * The total (possible) number of TAC channels can exceed the number
     * that fit between two macro time channels. This function returns the
     * effective number, i.e., the number of micro time channels between two
     * macro times. The micro time channels that are outside of this bound should
     * (usually) not be filled.
     *
     * @return effective_tac_channels (that fit between to macro times)
     */
    unsigned int get_effective_number_of_micro_time_channels(){
        double macro_time_resolution = get_macro_time_resolution();
        double micro_time_resolution = get_micro_time_resolution();
        return (unsigned int) std::floor(macro_time_resolution / micro_time_resolution);
    }

    /*!
     * Default constructor
     */
    TTTRHeader();
    TTTRHeader(int tttr_container_type);

    /// Copy constructor
    TTTRHeader(const TTTRHeader &p2);

    /*!
     * Copy assignment.
     *
     * Declared explicitly because the pimpl'd `json_data_` would otherwise
     * make the implicit one deleted, silently removing an operation the class
     * had before.
     */
    TTTRHeader& operator=(const TTTRHeader &p2);

    /*!
     * Constructor for the @class Header that takes a file pointer and the container
     * type of the file represented by the file pointer. The container type refers either to a PicoQuant (PQ) PTU or
     * HT3 file, or a BeckerHickl (BH) spc file. There are three different types of BH spc files SPC130,
     * SPC600_256 (256 bins in micro time) or SPC600_4096 (4096 bins in micro time).
     * PQ HT3 files may contain different TTTR record types depending on the counting device (HydraHarp, PicoHarp)
     * and firmware revision of the counting device. Similarly, PTU files support a diverse set of TTTR records.
     *
     * @param fpin the file pointer to the TTTR file
     * @param tttr_container_type the container type
     *
     */
    TTTRHeader(std::FILE *fpin, int tttr_container_type=0, bool close_file=false);
    TTTRHeader(std::string fn, int tttr_container_type=0);

    /// Out of line: `json_data_` points at an incomplete type here.
    ~TTTRHeader();

    /*!
     * @brief Reads the header of a PTU file and sets the reading routing.
     *
     * @param fpin File pointer to the PTU file.
     * @param tttr_record_type Output parameter for the TTTR record type.
     * @param json_data Output parameter for JSON data.
     * @param rewind Flag to indicate whether to rewind the file (default is true).
     * @return The position of the file pointer at the end of the header.
     */
    static size_t read_ptu_header(
            std::FILE *fpin,
            int &tttr_record_type,
            nlohmann::json &json_data,
            bool rewind = true
    );

    int read_photon_hdf5_setup(const char *fn);

    /*!
     * @brief Reads the header of an HT3 file and sets the reading routing.
     *
     * @param fpin File pointer to the HT3 file.
     * @param data Output parameter for JSON data.
     * @param rewind Flag to indicate whether to rewind the file (default is true).
     * @return The position of the file pointer at the end of the header.
     */
    static size_t read_ht3_header(
            std::FILE *fpin,
            nlohmann::json &data,
            bool rewind = true
    );

    /**
     * Reads and parses the header of an SM (Single molecule) record from the given file.
     * The parsed information is stored in the provided JSON object `j`.
     *
     * This function performs the following tasks:
     * 1. Adds a tag to the JSON object `j` for the record type.
     * 2. Reads and processes the header information from the file.
     *
     * @param file A pointer to the file from which the header is read.
     * @param j A reference to a nlohmann::json object where parsed information will be stored.
     * @return The file position after reading the header.
     */
    static size_t read_sm_header(FILE* file, nlohmann::json &j);

    /*!
     * @brief Reads the header of a Becker & Hickl SPC132 file and sets the reading routing.
     *
     * @param fpin File pointer to the SPC132 file.
     * @param data Output parameter for JSON data.
     * @param rewind Flag to indicate whether to rewind the file (default is true).
     * @return The position of the file pointer at the end of the header.
     */
    static size_t read_bh132_header(
            std::FILE *fpin,
            nlohmann::json &data,
            bool rewind = true
    );

    /*!
     * @brief Reads the header of a Becker & Hickl SPC-QC file.
     *
     * The header is the same 4 byte word as in an SPC-130 file, but the macro
     * time clock is stored in femtoseconds (see @ref bh_spcqc_header_t).
     * The micro time resolution cannot be derived from it because the QC
     * modules run their TAC independently of the macro time clock; a default
     * TAC range is assumed and replaced from the ".set" sidecar when present
     * (see @ref read_bh_set_file).
     *
     * @param fpin File pointer to the SPC-QC file.
     * @param data Output parameter for JSON data.
     * @param rewind Flag to indicate whether to rewind the file (default is true).
     * @return The position of the file pointer at the end of the header.
     */
    static size_t read_bh_spcqc_header(
            std::FILE *fpin,
            nlohmann::json &data,
            bool rewind = true
    );

    /*!
     * @brief Reads a Becker & Hickl .set file and extracts imaging parameters.
     *
     * Parses the BH .set file to extract SP_IMG_X (pixels per line),
     * SP_IMG_Y (lines per frame), and SP_PIX_CLK (pixel clock mode).
     * These values are stored in json_data under tags:
     *   - ImgHdr_PixX
     *   - ImgHdr_PixY
     *   - BH_UsePixelClock
     *
     * For SPC-QC containers SP_TAC_R (TAC range) and SP_ADC_RE (ADC
     * resolution) are read in addition and define the micro time resolution
     * (TTTRTagRes), which the 4 byte .spc header of those modules cannot carry.
     *
     * @param filename Path to the .set file
     * @return true if parsing succeeded, false otherwise
     */
    bool read_bh_set_file(const std::string& filename);

    /*!
     * @brief Writes a Becker & Hickl .set sidecar file with imaging parameters.
     *
     * The BH SPC record file cannot store the CLSM imaging geometry, which BH
     * software keeps in a companion `.set` file next to the `.spc`. This method
     * writes such a file from the imaging tags in @p header, i.e. the inverse of
     * @ref read_bh_set_file:
     *   - ImgHdr_PixX      -> SP_IMG_X
     *   - ImgHdr_PixY      -> SP_IMG_Y
     *   - BH_UsePixelClock -> SP_PIX_CLK
     *
     * Only the parameters present in the header are written. If the header
     * carries no imaging geometry (no ImgHdr_PixX/PixY), nothing is written and
     * the method returns false, so non-imaging measurements do not get a
     * meaningless sidecar.
     *
     * @param filename Path to the .set file to write.
     * @param header Header providing the imaging tags.
     * @return true if a .set file was written, false otherwise.
     */
    static bool write_bh_set_file(const std::string& filename, TTTRHeader* header);

    /*!
     * @brief Reads the header of a Carl Zeiss (CZ) Confocor3 file and sets the reading routing.
     *
     * @param fpin File pointer to the Confocor3 file.
     * @param data Output parameter for JSON data.
     * @param rewind Flag to indicate whether to rewind the file (default is true).
     * @return The position of the file pointer at the end of the header.
     */
    static size_t read_cz_confocor3_header(
            std::FILE *fpin,
            nlohmann::json &data,
            bool rewind = true
    );

    /*!
     * Write a spc132 header to a file
     *
     * WARNING: If the default write mode is "wb". Existing files are overwritten.
     *
     * @param fn filename
     * @param header pointer to the TTTRHeader object that is written to the file
     * @param modes the writing modes (default 'w+b')
     */
    static void write_spc132_header(
            std::string fn,
            TTTRHeader* header,
            std::string modes = "w"
    );

    /*!
     * Write a Becker & Hickl SPC-QC header to a file
     *
     * WARNING: If the default write mode is "wb". Existing files are overwritten.
     *
     * @param fn filename
     * @param header pointer to the TTTRHeader object that is written to the file
     * @param modes the writing modes (default 'w+b')
     */
    static void write_spcqc_header(
            std::string fn,
            TTTRHeader* header,
            std::string modes = "w"
    );

    /*!
     * @brief Ensure the header carries the minimal metadata a container needs.
     *
     * When a TTTR object is transcoded into a different container (or was built
     * from scratch) its header may lack tags that the target format's writer or
     * downstream readers require. This method fills in the essential metadata --
     * macro/micro time resolution, micro-time channel count, record encoding and
     * the record count -- from the best information already available in the
     * header, deriving sane fallbacks only when a value is genuinely missing.
     *
     * Existing tags are never overwritten, so metadata that survives a transcode
     * is preserved verbatim; only gaps are filled. This keeps written files
     * valid regardless of the source container.
     *
     * @param header The header to complete in place.
     * @param container_type The target container type (`*_CONTAINER`).
     * @param n_records The number of records that will be written.
     */
    static void ensure_minimal_tags(
            TTTRHeader* header,
            int container_type,
            size_t n_records
    );

    /*!
     * Write a PTU header to a file
     *
     * WARNING: If the default write mode is "wb". Existing files are overwritten.
     *
     * @param fn filename
     * @param header pointer to the TTTRHeader object that is written to the file
     * @param modes the writing modes (default 'wb')
     */
    static void write_ptu_header(
            std::string fn,
            TTTRHeader* header,
            std::string modes = "wb"
    );

    /*!
     * Write a HT3 header to a file
     *
     * WARNING: If the default write mode is "wb". Existing files are overwritten.
     *
     * @param fn filename
     * @param header pointer to the TTTRHeader object that is written to the file
     * @param modes the writing modes (default 'wb')
     */
    static void write_ht3_header(
            std::string fn,
            TTTRHeader* header,
            std::string modes = "wb"
    );

    /*!
     * Write a SM header to a file
     *
     * WARNING: If the default write mode is "wb". Existing files are overwritten.
     *
     * @param fn filename
     * @param header pointer to the TTTRHeader object that is written to the file
     * @param modes the writing modes (default 'wb')
     */
    static void write_sm_header(
            std::string fn,
            TTTRHeader* header,
            std::string modes = "wb"
    );

    /*!
     * Write a Carl Zeiss ConfoCor3 raw-data header to a file
     *
     * WARNING: If the default write mode is "wb". Existing files are overwritten.
     *
     * @param fn filename
     * @param header pointer to the TTTRHeader object that is written to the file
     * @param modes the writing modes (default 'wb')
     */
    static void write_cz_confocor3_header(
            std::string fn,
            TTTRHeader* header,
            std::string modes = "wb"
    );

    /*!
     * Get a representation of the TTTRHeader meta data as a JSON string
     *
     * @param tag_name name of requested tag (if no name is provided) the entire
     * information in the TTTRHeader is returned
     * @param idx index of the tag
     * @param indent an integer that controls the indent in the returned
     * JSON string
     * @return
     */
    std::string get_json(std::string tag_name="", int idx=-1, int indent=1);

    /*!
     * Set / update the TTTRHeader meta data using a JSON string
     *
     * @param json_string
     */
    void set_json(std::string json_string);

};


#endif //TTTRLIB_READHEADER_H
