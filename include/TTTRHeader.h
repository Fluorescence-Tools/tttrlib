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


#ifndef TTTRLIB_READHEADER_H
#define TTTRLIB_READHEADER_H

#include <stdlib.h>     /* malloc, calloc, realloc, exit, free */
#include <cstdint>
#include <cstdio>
#include <iostream>
#include <map>
#include <cmath> /* floor */
#include <string>
#include <string.h> /* strcmp */
#include <algorithm>
#include <vector>
#include <array>
#include <memory>
#include <numeric>

#include <boost/any.hpp>
#include <boost/filesystem.hpp>
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
const std::string TTTRNMicroTimes = "MeasDesc_NumberMicrotimes";   // The number of micro time channels
const std::string TTTRRecordType = "MeasDesc_RecordType";         // Internal record type (see tttrlib record type identifier definitions)
const std::string TTTRTagTTTRRecType = "TTResultFormat_TTTRRecType";
const std::string TTTRTagBits = "TTResultFormat_BitsPerRecord";    // Bits per TTTR record
const std::string FileTagEnd = "Header_End";                       // Always appended as last tag (BLOCKEND)

class TTTRHeader {

    friend class TTTR;

protected:
    // JSON object used to store all the header information
    nlohmann::json json_data;
    int tttr_container_type;
    int tttr_record_type;


    /*!
     * Marks the end of the header in the file (position in file)
     */
    size_t header_end = 0;

public:

    /*!
     * TTTR record type
     *
     * @return
     */
    int get_tttr_record_type(){
        return tttr_record_type;
    }

    /*!
     *
     * @param v record type
     */
    void set_tttr_record_type(int v){
        tttr_record_type = v;
    }

    /*!
     * The container type
     * @return
     */
    int get_tttr_container_type(){
        return tttr_container_type;
    }

    /*!
     *
     * @param v container type
     */
    void set_tttr_container_type(int v){
        tttr_container_type = v;
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
            ){
#ifdef VERBOSE_TTTRLIB
        std::clog << "-- GET_TAG" << name << std::endl;
#endif
        for (auto& it : json_data["tags"].items()) {
#ifdef VERBOSE_TTTRLIB
            std::clog << it << "\n";
#endif
            if(it.value()["name"] == name){
                if((idx < 0) || (idx == it.value()["idx"])){
                    return it.value();
                }
            }
        }
        std::cerr << "ERROR: TTTR-TAG " << name << ":" << idx << " not found." << std::endl;
        nlohmann::json re = {};
        return re;
    }

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
    ){
//#ifdef VERBOSE_TTTRLIB
//        std::clog << "FIND_TAG: " << name << ":" << idx << std::endl;
//#endif
        int tag_idx = -1;
        int curr_idx = 0;
        for (auto& it : json_data["tags"].items()) {
            if((it.value()["name"] == name) && (it.value()["idx"] == idx)){
                tag_idx = curr_idx;
                break;
            }
            curr_idx++;
        }
//#ifdef VERBOSE_TTTRLIB
//        std::clog << "Found at idx: " << tag_idx << std::endl;
//#endif
        return tag_idx;
    }

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
    ){
        nlohmann::json tag;
        tag["type"] = type;
        tag["name"] = name;
        tag["idx"] = idx;
        if(type == tyEmpty8){
            tag["value"] = nullptr;
        } else if(type == tyBool8) {
            tag["value"] = boost::any_cast<bool>(value);
        } else if((type == tyInt8) || (type == tyBitSet64) || (type == tyColor8)) {
            tag["value"] = boost::any_cast<int>(value);
        } else if((type == tyFloat8) || (type == tyTDateTime)) {
            tag["value"] = boost::any_cast<double>(value);
        } else if(type == tyFloat8Array) {
            tag["value"] = boost::any_cast<std::vector<double>>(value);
        } else if(type == tyAnsiString){
            tag["value"] = boost::any_cast<std::string>(value);
        } else if(type == tyWideString){
            //
            tag["value"] = boost::any_cast<std::string>(value);
        } else if(type == tyBinaryBlob){
            tag["value"] = boost::any_cast<std::vector<int32_t>>(value);
        } else{
            tag["value"] = std::to_string(boost::any_cast<int>(value));
        }
        int tag_idx = find_tag(json_data, name, idx);
        if(tag_idx < 0){
            json_data["tags"].emplace_back(tag);
        } else{
            json_data["tags"][tag_idx] = tag;
        }
#ifdef VERBOSE_TTTRLIB
        std::clog << "ADD_TAG: " << tag << std::endl;
#endif
    }

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
    double get_macro_time_resolution(){
        return get_tag(json_data, TTTRTagGlobRes)["value"];
    }

    /// Resolution for the micro time in nanoseconds
    double get_micro_time_resolution(){
        return get_tag(json_data, TTTRTagRes)["value"];
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
     * @return The TTTR container type of the associated TTTR file as a char
     */
    int getTTTRRecordType();

    /*!
     * Default constructor
     */
    TTTRHeader();
    TTTRHeader(int tttr_container_type) : TTTRHeader(){
        this->tttr_container_type = tttr_container_type;
    };

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
     * @param modes the writing modes (default 'wb')
     */
    static void write_spc132_header(
            std::string fn,
            TTTRHeader* header,
            std::string modes = "wb"
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
    std::string get_json(std::string tag_name="", int idx=-1, int indent=1){
        std::string s;
        if(tag_name.empty()){
            s = json_data.dump(indent);
        } else{
            int tag_idx = find_tag(json_data, tag_name, idx);
            s = json_data["tags"][tag_idx].dump(indent);
        }
        return s;
    }

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
