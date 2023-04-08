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
#include <fstream> /* ifstream */

#include <boost/any.hpp>
//#include <boost/filesystem.hpp>
#include <boost/locale.hpp>

#include "json.hpp"

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
const std::string FileTagEnd = "Header_End";                       // Always appended as last tag (BLOCKEND)


class TTTRHeader {

    friend class TTTR;

protected:
    // JSON object used to store all the header information
    nlohmann::json json_data;

    /*!
     * Marks the end of the header in the file (position in file)
     */
    size_t header_end = 0;

public:

    /*!
     * @return The TTTR container type of the associated TTTR file as a char
     */
    int get_tttr_record_type(){
        return (int) json_data[TTTRRecordType];
    }

    /*!
     *
     * @param v record type
     */
    void set_tttr_record_type(int v){
        json_data[TTTRRecordType] = v;
    }

    /*!
     * The container type
     * @return
     */
    int get_tttr_container_type(){
        return (int) json_data[TTTRContainerType];
    }

    /*!
     *
     * @param v container type
     */
    void set_tttr_container_type(int v){
        json_data[TTTRContainerType] = v;
    }

    /*!
     * Get a tag / entry from the meta data list in a JSON dict
     *
     * @param json_data
     * @param name
     * @param idx
     * @return
     */
    static nlohmann::json get_tag(
            nlohmann::json json_data,
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
            boost::any value,
            unsigned int type = tyAnsiString,
            int idx = -1
    );

    /*!
     * Stores the bytes per TTTR record of the associated TTTR file
     * This attribute is changed when a header is read
    */
    size_t get_bytes_per_record(){
        return (size_t) get_tag(json_data, TTTRTagBits)["value"] / 8;
    }

    size_t end() const{
        return header_end;
    }

    /*!
     * Number of meta data entries
     */
    size_t size(){
        return json_data["tags"].size();
    }

    nlohmann::json& operator[](std::size_t idx){
        return json_data["tags"][idx];
    }

    const nlohmann::json& operator[](std::size_t idx) const {
        return json_data["tags"][idx];
    }

    /*!
     * The total (possible) number of micro time channels.
     *
     * The number of TAC channels (TAC - Time to analog converter) refers to
     * the number of micro time channels.
     */
     unsigned int get_number_of_micro_time_channels(){
        return (unsigned int) get_tag(json_data, TTTRNMicroTimes)["value"];
     }

    /// Resolution for the macro time in nanoseconds
    double get_macro_time_resolution();

    /// Resolution for the micro time in nanoseconds
    double get_micro_time_resolution(){
        return get_tag(json_data, TTTRTagRes)["value"];
    }

    /// Duration of a pixel in LSM in units of macro time clock
    int get_pixel_duration(){
        double pixel_duration_d = TTTRHeader::get_tag(
                json_data, "$TimePerPixel")["value"];
        double global_res = TTTRHeader::get_tag(
                json_data, "MeasDesc_GlobalResolution")["value"];
        long pixel_duration = std::round(pixel_duration_d / global_res);
        return pixel_duration;
    }

    /// Duration of a line in LSM in units of macro time clock
    int get_line_duration(){
        double pixel_duration_d = TTTRHeader::get_tag(
                json_data, "$TimePerPixel")["value"];
        double global_res_d = TTTRHeader::get_tag(
                json_data, "MeasDesc_GlobalResolution")["value"];
        double n_pixel = TTTRHeader::get_tag(json_data, "ImgHdr_PixX")["value"];
        return std::ceil((pixel_duration_d * n_pixel) / global_res_d);
    }

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
    ~TTTRHeader() = default;

    /*! Reads the header of a ptu file and sets the reading routing for
     *
     * @param fpin
     * @param rewind
     * @param tttr_record_type
     * @param json_data
     * @param macro_time_resolution
     * @param micro_time_resolution
     * @return The position of the file pointer at the end of the header
     */
    static size_t read_ptu_header(
            std::FILE *fpin,
            int &tttr_record_type,
            nlohmann::json &json_data,
            bool rewind = true
    );

    /*! Reads the header of a ht3 file and sets the reading routing for
     *
     * @param fpin
     * @param rewind
     * @param tttr_record_type
     * @param data
     * @return The position of the file pointer at the end of the header
     */
    static size_t read_ht3_header(
            std::FILE *fpin,
            nlohmann::json &data,
            bool rewind=true
    );

    /*! Reads the header of a Becker&Hickel SPC132 file and sets the reading routing
     *
     * @param fpin
     * @param rewind
     * @param tttr_record_type
     * @param data JSON dictionary that will contain the header information
     */
    static size_t read_bh132_header(
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
    void set_json(std::string json_string){
        json_data = nlohmann::json::parse(json_string);
    }

};


#endif //TTTRLIB_READHEADER_H
