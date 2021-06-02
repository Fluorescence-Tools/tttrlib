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

#include "TTTR.h"
#include "TTTRRange.h"
#include "TTTRHeader.h"

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif


TTTRHeader::TTTRHeader() :
        tttr_container_type(0),
        tttr_record_type(-1),
        header_end(0)
{
    json_data["tags"] = nlohmann::json::array();
#if VERBOSE_TTTRLIB
    std::clog << "-- TTTRHeader::TTTRHeader" << std::endl;
#endif
}

TTTRHeader::TTTRHeader(const TTTRHeader &p2)
{
#if VERBOSE_TTTRLIB
    std::clog << "-- TTTRHeader::TTTRHeader - Copy constructor" << std::endl;
#endif
    json_data = p2.json_data;
    header_end = p2.header_end;
    tttr_record_type = p2.tttr_record_type;
    tttr_container_type = p2.tttr_container_type;
}

TTTRHeader::TTTRHeader(
        std::FILE *fpin,
        int tttr_container_type,
        bool close_file
        ) :
        TTTRHeader(tttr_container_type)
{
#if VERBOSE_TTTRLIB
    std::clog << "-- TTTRHeader::TTTRHeader - Opening file" << std::endl;
    std::clog << "reading header" << std::endl;
#endif
    if(tttr_container_type == PQ_PTU_CONTAINER){
        header_end = read_ptu_header(fpin, tttr_record_type, json_data);
    } else if(tttr_container_type == PQ_HT3_CONTAINER){
        header_end = read_ht3_header(fpin, json_data);
        tttr_record_type = get_tag(json_data, TTTRRecordType)["value"];
    } else if(tttr_container_type == BH_SPC600_256_CONTAINER){
        header_end = 0;
        add_tag(json_data, TTTRTagGlobRes, 1.0, tyFloat8);
        add_tag(json_data, TTTRNMicroTimes, 256, tyInt8);
        add_tag(json_data, TTTRTagBits, 32, tyInt8);
        tttr_record_type = BH_RECORD_TYPE_SPC600_256;
    } else if(tttr_container_type == BH_SPC600_4096_CONTAINER){
        header_end = 0;
        add_tag(json_data, TTTRTagGlobRes, 1.0, tyFloat8);
        add_tag(json_data, TTTRNMicroTimes, 4096, tyInt8);
        add_tag(json_data, TTTRTagBits, 48, tyInt8);
        tttr_record_type = BH_RECORD_TYPE_SPC600_4096;
    } else if(tttr_container_type == BH_SPC130_CONTAINER){
        header_end = read_bh132_header(fpin, json_data);
        tttr_record_type = (int) BH_RECORD_TYPE_SPC130;
    } else{
        header_end = 0;
        add_tag(json_data, TTTRTagBits, 32, tyInt8);
        tttr_record_type = BH_RECORD_TYPE_SPC130;
    }
    if(close_file) fclose(fpin);
#if VERBOSE_TTTRLIB
    std::clog << "End of header: " << header_end << std::endl;
#endif
}

TTTRHeader::TTTRHeader(
        std::string fn,
        int tttr_container_type
) : TTTRHeader(fopen(fn.c_str(), "r"), tttr_container_type, true) {}

int TTTRHeader::getTTTRRecordType(){
    return this->tttr_record_type;
}

size_t TTTRHeader::read_bh132_header(
        std::FILE *fpin,
        nlohmann::json &data,
        bool rewind
){
    if(rewind) std::fseek(fpin, 0, SEEK_SET);
    bh_spc132_header_t rec;
    fread(&rec, sizeof(rec),1, fpin);
    double mt_clk = (double) rec.macro_time_clock / 10.0e9; // divide by 10.0e9 to get units of seconds
    double mi_clk = mt_clk / 4096.0;
    add_tag(data, TTTRTagRes, mi_clk, tyFloat8);
    add_tag(data, TTTRTagGlobRes, mt_clk, tyFloat8);
    add_tag(data, TTTRNMicroTimes, 4096, tyInt8);
    add_tag(data, TTTRTagBits, 32, tyInt8);

#if VERBOSE_TTTRLIB
    std::clog << "-- BH132 header reader " << std::endl;
    std::clog << "-- macro_time_resolution: " << mt_clk << std::endl;
    std::clog << "-- micro_time_resolution: " << mi_clk << std::endl;
#endif
    return 4;
}

size_t TTTRHeader::read_ht3_header(
        std::FILE *fpin,
        nlohmann::json &data,
        bool rewind
) {
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- READ_HT3_HEADER" << std::endl;
#endif
    if(rewind) std::fseek(fpin, 0, SEEK_SET);
    // Header of HT3 file
    pq_ht3_Header_t ht3_header_begin;
    fread(&ht3_header_begin, 1, sizeof(ht3_header_begin), fpin);
    if(strncmp(ht3_header_begin.FormatVersion, "1.0", 3) != 0){
        std::cerr << "WARNING: Only HT3 files in version 1.0 supported." << std::endl;
    }
    add_tag(data, "Ident", std::string(ht3_header_begin.Ident));
    add_tag(data, "FormatVersion", std::string(ht3_header_begin.FormatVersion));
    add_tag(data, "CreatorName", std::string(ht3_header_begin.CreatorName));
    add_tag(data, "CreatorVersion", std::string(ht3_header_begin.CreatorVersion));
    add_tag(data, "FileTime", std::string(ht3_header_begin.FileTime));
    add_tag(data, "Comment", std::string(ht3_header_begin.CommentField));
    add_tag(data, "NumberOfCurves", ht3_header_begin.NumberOfCurves, tyInt8);
    add_tag(data, TTTRTagBits, ht3_header_begin.BitsPerRecord, tyInt8);
    add_tag(data, "ActiveCurve", ht3_header_begin.ActiveCurve, tyInt8);
    add_tag(data, "MeasurementMode", ht3_header_begin.MeasurementMode, tyInt8);
    add_tag(data, "SubMode", ht3_header_begin.SubMode, tyInt8);
    add_tag(data, "Binning", ht3_header_begin.Binning, tyInt8);
    add_tag(data, "Resolution", ht3_header_begin.Resolution, tyFloat8);
    add_tag(data, "Offset", ht3_header_begin.Offset, tyInt8);
    add_tag(data, "AquisitionTime", ht3_header_begin.AquisitionTime, tyInt8);
    add_tag(data, "StopAt", (int) ht3_header_begin.StopAt, tyInt8);
    add_tag(data, "StopOnOvfl", (bool) ht3_header_begin.StopAt, tyBool8);
    add_tag(data, "Restart", (bool) ht3_header_begin.Restart, tyBool8);
    add_tag(data, "DispLinLog", (bool) ht3_header_begin.DispLinLog, tyBool8);
    add_tag(data, "DispTimeFrom", ht3_header_begin.DispTimeFrom, tyInt8);
    add_tag(data, "DispTimeTo", ht3_header_begin.DispTimeTo, tyInt8);
    add_tag(data, "DispCountsFrom", ht3_header_begin.DispCountsFrom, tyInt8);
    add_tag(data, "DispCountsTo", ht3_header_begin.DispCountsTo, tyInt8);

    pq_ht3_ChannelHeader_t channel_settings;
    for(int i=0; i<ht3_header_begin.InpChansPresent; i++){
        if(fread(&channel_settings, 1, sizeof(channel_settings), fpin) == sizeof(channel_settings)){
            add_tag(data, "InputCFDLevel", channel_settings.InputCFDLevel, tyInt8, i);
            add_tag(data, "InputCFDZeroCross", channel_settings.InputCFDZeroCross, tyInt8, i);
            add_tag(data, "InputOffset", channel_settings.InputOffset, tyInt8, i);
            add_tag(data, "InputRate", channel_settings.InputRate, tyInt8, i);
        }
    }

    // pq_ht3_TTModeHeader_t
    pq_ht3_TTModeHeader_t tt_mode_hdr;
    fread(&tt_mode_hdr, 1, sizeof(tt_mode_hdr), fpin);
    add_tag(data, "SyncRate", tt_mode_hdr.SyncRate, tyInt8);
    add_tag(data, "StopAfter", tt_mode_hdr.StopAfter, tyInt8);
    add_tag(data, "StopReason", tt_mode_hdr.StopReason, tyInt8);
    add_tag(data, "ImgHdrSize", tt_mode_hdr.ImgHdrSize, tyInt8);
    add_tag(data, "nRecords", (int) tt_mode_hdr.nRecords, tyInt8);

    // ImgHdr
//    fseek(fpin, (long) tt_mode_hdr.ImgHdrSize, SEEK_CUR);
    int ImgHdrSize = tt_mode_hdr.ImgHdrSize;
    if(ImgHdrSize > 0){
        auto imgHdr_array = (int32_t*) calloc(ImgHdrSize, sizeof(int32_t));
        fread(imgHdr_array, sizeof(int32_t), ImgHdrSize, fpin);
        std::vector<int32_t> v;
        for (int i=0; i<ImgHdrSize; i++) {
            v.emplace_back(imgHdr_array[i]);
        };
        free(imgHdr_array);
        add_tag(data, "ImgHdr", v, tyBinaryBlob);
        add_tag(data, "ImgHdr", v, tyBinaryBlob);

        add_tag(data, "ImgHdr_Frame", v[2] + 1, tyInt8);
        add_tag(data, "ImgHdr_LineStart", v[3], tyInt8);
        add_tag(data, "ImgHdr_LineStop", v[4], tyInt8);
        add_tag(data, "ImgHdr_PixX", v[6], tyInt8);
        add_tag(data, "ImgHdr_PixY", v[7], tyInt8);
    }

    double resolution = std::max(1.0, ht3_header_begin.Resolution) * 1e-12;
    add_tag(data, TTTRTagRes, resolution, tyFloat8);

    // TODO: add identification of HydraHarp HHT3v1 files
    std::cout << "FormatVersion:-" << get_tag(data, "FormatVersion")["value"] << "-" << std::endl;
    if (get_tag(data, "Ident")["value"] == "HydraHarp") {
        if(get_tag(data, "FormatVersion")["value"] == "1.0"){
#ifdef VERBOSE_TTTRLIB
            std::clog << "Record reader:" << "PQ_RECORD_TYPE_HHT3v1" << std::endl;
#endif
            add_tag(data, TTTRRecordType, (int) PQ_RECORD_TYPE_HHT3v1, tyInt8);
        } else{
#ifdef VERBOSE_TTTRLIB
            std::clog << "Record reader:" << "PQ_RECORD_TYPE_HHT3v2" << std::endl;
#endif
            add_tag(data, TTTRRecordType, (int) PQ_RECORD_TYPE_HHT3v2, tyInt8);
        }
    } else {
#ifdef VERBOSE_TTTRLIB
        std::clog << "Record reader:" << "PQ_RECORD_TYPE_PHT3" << std::endl;
#endif
        add_tag(data, TTTRRecordType, (int) PQ_RECORD_TYPE_PHT3, tyInt8);
    }
    // Effective number of micro time channels
    // TODO: divide by binning factor
    add_tag(data, TTTRNMicroTimes, (int) 32768 / std::max(1, ht3_header_begin.Binning), tyInt8);
    //return 880; // guessed by inspecting several ht3 files
    return (size_t) ftell(fpin);
}


size_t TTTRHeader::read_ptu_header(
        std::FILE *fpin,
        int &tttr_record_type,
        nlohmann::json &json_data,
        bool rewind
) {
#if VERBOSE_TTTRLIB
    std::clog << "-- TTTRHeader::read_ptu_header" << std::endl;
#endif
    /// The version of the PTU file
    char version[8];
    char Magic[8];
    if(rewind) std::fseek(fpin, 0, SEEK_SET);

    // variables for reading
    uint64_t tmp;
    char buffer_out[1024];
    char *AnsiBuffer;
    wchar_t *WideBuffer;
    std::string strFromChar;
    tag_head_t TagHead;
    uint64_t file_type = 0;
    double *b; std::vector<double> vec;
    // read the header

    fread(&Magic, 1, sizeof(Magic), fpin);
    // check if file is a PQ-PTU file
    if (strncmp(Magic, "PQTTTR", 6) != 0) {
        throw std::string("\nWrong Magic, this is not a PTU file.");
    }

    // Get the version
    tmp = fread(&version, 1, sizeof(version), fpin);
    if (tmp != sizeof(version)) {
        throw std::string("\nerror reading header, aborted.");
    }
    sprintf(buffer_out, "%s", version);
    json_data["Tag Version"] = buffer_out;

#if VERBOSE_TTTRLIB
    std::clog << "PTU ID:" << Magic << std::endl;
    std::clog << "Tag version:" << json_data["Tag Version"] << std::endl;
    std::clog << "Reading keys..." << std::endl;
#endif
    do {
        uint64_t Result;
        // This loop all header items and displays the identifier and the associated
        // value, independent of what the tags mean in detail. Only some selected
        // items are explicitly retrieved and kept in memory because they are
        // needed to subsequently interpret the TTTR record data.
        Result = fread(&TagHead, 1, sizeof(TagHead), fpin);
        if (Result != sizeof(TagHead))
            throw std::string("Incomplete File.");
        // Record type
        if (TTTRTagTTTRRecType == TagHead.Ident)
            file_type = TagHead.TagValue;
        std::string key = TagHead.Ident;
#if VERBOSE_TTTRLIB
        std::clog << key << ":" << TagHead.Typ << ":" << TagHead.TagValue << ";" << std::endl;
#endif
        // The header end tag is skipped.
        if(FileTagEnd != TagHead.Ident){
            switch (TagHead.Typ) {
                case tyEmpty8:
                    add_tag(json_data, key, nullptr, TagHead.Typ, TagHead.Idx);
                    break;
                case tyBool8:
                    add_tag(json_data, key, *(bool *) &(TagHead.TagValue), TagHead.Typ, TagHead.Idx);
                    break;
                case tyInt8:
                case tyBitSet64:
                case tyColor8:
                    add_tag(json_data, key, *(int *) &(TagHead.TagValue), TagHead.Typ, TagHead.Idx);
                    break;
                case tyFloat8:
                    add_tag(json_data, key, *(double *) &(TagHead.TagValue), TagHead.Typ, TagHead.Idx);
                    break;
                case tyTDateTime:
                    double time;
                    time = *(double *) &(TagHead.TagValue); time -= 25569; time *= 86400;
                    add_tag(json_data, key, time, TagHead.Typ, TagHead.Idx);
                    break;
                case tyFloat8Array:
                    // sprintf(buffer_out, "<Float Array with %llu Entries>", TagHead.TagValue / sizeof(double));
                    // only seek the Data, if one needs the data, it can be loaded here
//                fseek(fpin, (long) TagHead.TagValue, SEEK_CUR);
                    b = (double *) calloc((size_t) TagHead.TagValue, 1);
                    fread(b, 1, (size_t) TagHead.TagValue, fpin);
                    vec.assign(b, b + TagHead.TagValue);
                    add_tag(json_data, key, vec, TagHead.Typ, TagHead.Idx);
                    free(b);
                    break;
                case tyAnsiString:
                    AnsiBuffer = (char *) calloc((size_t) TagHead.TagValue, 1);
                    Result = fread(AnsiBuffer, 1, (size_t) TagHead.TagValue, fpin);
                    if (Result != TagHead.TagValue) {
                        free(AnsiBuffer);
                        throw std::string("Incomplete File.");
                    }
                    // AnsiString seems to be Latin 1 - convert to UTF8 to use in JSON lib
                    add_tag(
                            json_data, key,
                            boost::locale::conv::to_utf<char>(AnsiBuffer, "Latin1"),
                            TagHead.Typ, TagHead.Idx);
                    free(AnsiBuffer);
                    break;
                case tyWideString:
                    WideBuffer = (wchar_t *) calloc((size_t) TagHead.TagValue, 1);
                    Result = fread(WideBuffer, 1, (size_t) TagHead.TagValue, fpin);
                    if (Result != TagHead.TagValue) {
                        free(WideBuffer);
                        throw std::string("Incomplete File");
                    }
                    add_tag(
                            json_data, key,
                            boost::locale::conv::to_utf<wchar_t>(AnsiBuffer, "Latin1"),
                            TagHead.Typ, TagHead.Idx);
                    free(WideBuffer);
                    break;
                case tyBinaryBlob:
                    std::cerr << "ERROR: PTU tyBinaryBlob not supported" << std::endl;
                    //sprintf(buffer_out, "<Binary Blob contains %llu Bytes>", TagHead.TagValue);
                    // only seek the Data, if one needs the data, it can be loaded here
                    fseek(fpin, (long) TagHead.TagValue, SEEK_CUR);
                    break;
                default:
                    throw std::string("Illegal Type identifier! Broken file?");
            }
        }
    } while (FileTagEnd != TagHead.Ident);
    // select the reading routine
    switch (file_type) {
        case rtPicoHarpT2:
            tttr_record_type = PQ_RECORD_TYPE_PHT2;
            break;
        case rtPicoHarpT3:
            tttr_record_type = PQ_RECORD_TYPE_PHT3;
            break;
        case rtHydraHarpT2:
            tttr_record_type = PQ_RECORD_TYPE_HHT2v1;
            break;
        case rtMultiHarpNT2:
        case rtHydraHarp2T2:
        case rtTimeHarp260NT2:
        case rtTimeHarp260PT2:
            tttr_record_type = PQ_RECORD_TYPE_HHT2v2;
            break;
        case rtHydraHarpT3:
        case rtMultiHarpNT3:
        case rtHydraHarp2T3:
        case rtTimeHarp260NT3:
        case rtTimeHarp260PT3:
            tttr_record_type = PQ_RECORD_TYPE_HHT3v2;
            break;
        default:
            std::cerr << "PTU file type not supported." << std::endl;
    }
    // Effective number of micro time channels
    try{
        int bining_factor = get_tag(json_data, "MeasDesc_BinningFactor")["value"];
        add_tag(json_data, TTTRNMicroTimes, 32768 / bining_factor, tyInt8, true); //2**15
    } catch (...) {
        std::cerr << "ERROR: MeasDesc_BinningFactor not found." << std::endl;
    }
    return (size_t) ftell(fpin);
}

void TTTRHeader::write_spc132_header(
        std::string fn,
        TTTRHeader* header,
        std::string mode
        ){
    // write header
    FILE* fp = fopen(fn.c_str(), mode.c_str());
    bh_spc132_header_t head;
    nlohmann::json tag = get_tag(header->json_data, TTTRTagRes);
    head.invalid = true;
    head.macro_time_clock = (unsigned) ((double) tag["value"] * 10.e9);
    fwrite(&head, sizeof(bh_spc132_header_t), 1, fp);
    fclose(fp);
}

void TTTRHeader::write_ptu_header(std::string fn, TTTRHeader* header, std::string modes){
    #if VERBOSE_TTTRLIB
    std::clog << "TTTRHeader::write_ptu_header" << std::endl;
    #endif
    // Check for existing file
    if(boost::filesystem::exists(fn)){
        std::clog << "WARNING: File exists" << fn << "." << std::endl;
    }
    // write header information that is not in header tags
    FILE* fp = fopen(fn.c_str(), modes.c_str());
    // Write identifier for PTU files
    char version[8]; std::string version_str;
    char Magic[8] = "PQTTTR";
    fwrite(&Magic, 1, sizeof(Magic), fp);
    try {
        version_str = header->json_data["Tag Version"];
    } catch (...) {
        std::clog << "WARNING: No PTU version defined in header using default" << std::endl;
        version_str = "0      ";
    }
    strcpy(version, version_str.c_str());
    fwrite(&version, sizeof(version), 1, fp);
    // write header tags
    // variables for writing
    tag_head_t TagHead;
    double tmp_d;
    uint64_t tmp_i;
    uint64_t tmp_s;
    std::string tmp_str;
    std::wstring tmp_wstr;
    char *AnsiBuffer;
    wchar_t *WideBuffer;
    // Flag to check if the header end tag was written
    bool header_end_written = false;

    for(auto &it: header->json_data["tags"].items()){
        auto tag = it.value();
#if VERBOSE_TTTRLIB
        std::clog << tag << std::endl;
#endif
        tmp_str = tag["name"];
        strcpy(TagHead.Ident, tmp_str.c_str());
        TagHead.Idx = tag["idx"];
        TagHead.Typ = tag["type"];
        if(tmp_str == FileTagEnd)
            header_end_written = true;
        switch (TagHead.Typ) {
            // In these cases the tags have the same number of bits
            case tyTDateTime:
                tmp_d = tag["value"];
                tmp_d /= 86400.0; tmp_d += 25569.0;
                TagHead.TagValue = *(uint64_t *) &(tmp_d);
                fwrite(&TagHead, sizeof(TagHead), 1, fp);
                break;
            case tyEmpty8:
                TagHead.TagValue = 0;
                fwrite(&TagHead, sizeof(TagHead), 1, fp);
                break;
            case tyBool8:
                TagHead.TagValue = (int) tag["value"];
                fwrite(&TagHead, sizeof(TagHead), 1, fp);
                break;
            case tyInt8:
            case tyBitSet64:
            case tyColor8:
                TagHead.TagValue = tag["value"];
                fwrite(&TagHead, sizeof(TagHead), 1, fp);
                break;
            case tyFloat8:
                tmp_d = tag["value"];
                TagHead.TagValue = *(uint64_t *) &(tmp_d);
                fwrite(&TagHead, sizeof(TagHead), 1, fp);
                break;
            // Arrays need to be treated differently
            case tyFloat8Array:
                // write the tag that defines the type and the size of the
                // following data
                tmp_s = tag["value"].size();
                TagHead.TagValue = *(uint64_t *) &(tmp_s);
                fwrite(&TagHead, sizeof(TagHead), 1, fp);
                // write the data
                for(auto &it_vec: tag["value"].items()){
                    tmp_i = *(uint64_t *) &it_vec.value();
                    fwrite(&tmp_i, 1, sizeof(uint64_t), fp);
                }
                break;
            case tyAnsiString:
                // write tag that marks the beginning of tyAnsiString
                tmp_str = tag["value"];
                TagHead.TagValue = tmp_str.size() * sizeof(char);
                fwrite(&TagHead, sizeof(TagHead), 1, fp);
                AnsiBuffer = (char*) malloc(TagHead.TagValue);
                strcpy(AnsiBuffer, tmp_str.c_str());
                fwrite(AnsiBuffer, sizeof(char), tmp_str.size(), fp);
                free(AnsiBuffer);
                break;
            case tyWideString:
                std::cerr << "ERROR: writing of tyWideString currently not supported" << std::endl;
//                // write tag that marks the beginning of tyAnsiString
//                tmp_wstr = tag["value"];
//                TagHead.TagValue = tmp_str.size() * sizeof(wchar_t);
//                fwrite(&TagHead, sizeof(TagHead), 1, fp);
//                WideBuffer = (wchar_t*) malloc(TagHead.TagValue);
//                wcscpy(WideBuffer, tmp_wstr.c_str());
//                fwrite(WideBuffer, sizeof(wchar_t), tmp_str.size(), fp);
//                free(WideBuffer);
                break;
            case tyBinaryBlob:
                std::cerr << "ERROR: writing of tyBinaryBlob currently not supported" << std::endl;
                break;
            default:
                throw std::string("Tag type not supported");
        }
    }
    if(!header_end_written){
#if VERBOSE_TTTRLIB
        std::clog << "Header_End is missing. Adding Header_End to tag list." << std::endl;
#endif
        TagHead.TagValue = 0;
        strcpy(TagHead.Ident, FileTagEnd.c_str());
        TagHead.Idx = -1;
        fwrite(&TagHead, sizeof(TagHead), 1, fp);
    }
    fclose(fp);
}

void TTTRHeader::write_ht3_header(
        std::string fn,
        TTTRHeader* header,
        std::string modes
        ){
    std::clog << "WARNING: HT3 header not fully supported." << std::endl;
}