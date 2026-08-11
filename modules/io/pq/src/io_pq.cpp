// SPDX-License-Identifier: BSD-3-Clause
#include "io_pq.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <vector>

#include "ByteOrder.h"
#include "FileIO.h"
#include "TTTRTags.h"
#include "Verbose.h"

namespace tttrlib {
namespace io {

size_t read_ptu_header(
        std::FILE *fpin,
        int &tttr_record_type,
        nlohmann::json &json_data,
        bool rewind
) {
if (is_verbose()) {
    std::clog << "-- TTTRHeader::read_ptu_header" << std::endl;
}
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
    if (strncmp(Magic, "PQTTTR", 6) != 0) {
        throw std::string("\nWrong Magic, this is not a PTU file.");
    }

    tmp = fread(&version, 1, sizeof(version), fpin);
    if (tmp != sizeof(version)) {
        throw std::string("\nerror reading header, aborted.");
    }
    sprintf(buffer_out, "%s", version);
    json_data["Tag Version"] = buffer_out;

if (is_verbose()) {
    std::clog << "PTU ID:" << Magic << std::endl;
    std::clog << "Tag version:" << json_data["Tag Version"] << std::endl;
    std::clog << "Reading keys..." << std::endl;
}
    do {
        uint64_t Result;
        Result = fread(&TagHead, 1, sizeof(TagHead), fpin);
        if (Result != sizeof(TagHead))
            throw std::string("Incomplete File.");
        if (TTTRTagTTTRRecType == TagHead.Ident)
            file_type = TagHead.TagValue;
        std::string key = TagHead.Ident;
if (is_verbose()) {
        std::clog << key << ":" << TagHead.Typ << ":" << TagHead.TagValue << ";" << std::endl;
}
        if (FileTagEnd != TagHead.Ident) {
            if (TagHead.Typ == tyEmpty8) {
                add_tag(json_data, key, nullptr, TagHead.Typ, TagHead.Idx);
            } else if (TagHead.Typ == tyBool8) {
                add_tag(json_data, key, *(bool *) &(TagHead.TagValue), TagHead.Typ, TagHead.Idx);
            } else if (TagHead.Typ == tyInt8 || TagHead.Typ == tyBitSet64 || TagHead.Typ == tyColor8) {
                add_tag(json_data, key, *(int *) &(TagHead.TagValue), TagHead.Typ, TagHead.Idx);
            } else if (TagHead.Typ == tyFloat8) {
                add_tag(json_data, key, *(double *) &(TagHead.TagValue), TagHead.Typ, TagHead.Idx);
            } else if (TagHead.Typ == tyTDateTime) {
                double time = *(double *) &(TagHead.TagValue); time -= 25569; time *= 86400;
                add_tag(json_data, key, time, TagHead.Typ, TagHead.Idx);
            } else if (TagHead.Typ == tyFloat8Array) {
                b = (double *) calloc((size_t) TagHead.TagValue, 1);
                fread(b, 1, (size_t) TagHead.TagValue, fpin);
                vec.assign(b, b + TagHead.TagValue);
                add_tag(json_data, key, vec, TagHead.Typ, TagHead.Idx);
                free(b);
            } else if (TagHead.Typ == tyAnsiString) {
                AnsiBuffer = (char *) calloc((size_t) TagHead.TagValue, 1);
                Result = fread(AnsiBuffer, 1, (size_t) TagHead.TagValue, fpin);
                if (Result != TagHead.TagValue) {
                    free(AnsiBuffer);
                    throw std::string("Incomplete File.");
                }
                add_tag(json_data, key, AnsiBuffer, TagHead.Typ, TagHead.Idx);
                free(AnsiBuffer);
            } else if (TagHead.Typ == tyWideString) {
                size_t buffer_size = TagHead.TagValue;
                WideBuffer = (wchar_t *) calloc((size_t) buffer_size, 1);
                Result = fread(WideBuffer, 1, (size_t) TagHead.TagValue, fpin);
                if (Result != TagHead.TagValue) {
                    free(WideBuffer);
                    throw std::string("Incomplete File");
                } else{
                    add_tag(json_data, key, WideBuffer, TagHead.Typ, TagHead.Idx);
                    free(WideBuffer);
                }
            } else if (TagHead.Typ == tyBinaryBlob) {
                // Kept, not skipped. A blob is opaque here and this reader can
                // do nothing with it -- but dropping it silently loses whatever
                // the instrument wrote, and a container that stores the header
                // cannot store what the reader threw away. One int32 per byte
                // is the representation add_tag already defines for this type.
                const size_t n = (size_t) TagHead.TagValue;
                std::vector<int32_t> blob(n, 0);
                if (n > 0) {
                    std::vector<unsigned char> raw(n, 0);
                    Result = fread(raw.data(), 1, n, fpin);
                    if (Result != n) throw std::string("Incomplete File");
                    for (size_t bi = 0; bi < n; bi++) blob[bi] = (int32_t) raw[bi];
                }
                add_tag(json_data, key, blob, TagHead.Typ, TagHead.Idx);
            } else {
                throw std::string("Illegal Type identifier! Broken file?");
            }
        }
    } while (FileTagEnd != TagHead.Ident);

    if (file_type == rtPicoHarpT2) {
        tttr_record_type = PQ_RECORD_TYPE_PHT2;
    } else if (file_type == rtPicoHarpT3) {
        tttr_record_type = PQ_RECORD_TYPE_PHT3;
    } else if (file_type == rtHydraHarpT2) {
        tttr_record_type = PQ_RECORD_TYPE_HHT2v1;
    } else if (file_type == rtMultiHarpT2) {
        tttr_record_type = PQ_RECORD_TYPE_GENERIC_T2;
    } else if (
            file_type == rtHydraHarp2T2 ||
            file_type == rtTimeHarp260NT2 ||
            file_type == rtTimeHarp260PT2
    ) {
        tttr_record_type = PQ_RECORD_TYPE_HHT2v2;
    } else if (file_type == rtHydraHarpT3) {
        tttr_record_type = PQ_RECORD_TYPE_HHT3v1;
    } else if (file_type == rtMultiHarpT3) {
        tttr_record_type = PQ_RECORD_TYPE_GENERIC_T3;
    } else if (
            file_type == rtHydraHarp2T3 ||
            file_type == rtTimeHarp260NT3 ||
            file_type == rtTimeHarp260PT3
    ) {
        tttr_record_type = PQ_RECORD_TYPE_HHT3v2;
    } else {
        std::cerr << "PTU file with undefined TTTRTagTTTRRecType." << std::endl;
        tttr_record_type = PQ_RECORD_TYPE_HHT3v2;
    }

    try {
        int bining_factor = get_tag(json_data, "MeasDesc_BinningFactor")["value"];
        if (bining_factor < 1) bining_factor = 1;
        add_tag(json_data, TTTRNMicroTimes, 32768 / bining_factor, tyInt8, true);
    } catch (...) {
        std::cerr << "ERROR: MeasDesc_BinningFactor not found." << std::endl;
}
    return static_cast<size_t>(ftell64(fpin));
}

size_t read_ht3_header(
        std::FILE *fpin,
        nlohmann::json &data,
        bool rewind
) {
if (is_verbose()) {
    std::clog << "-- READ_HT3_HEADER" << std::endl;
}
    if(rewind) std::fseek(fpin, 0, SEEK_SET);
    // Header of HT3 file
    pq_ht3_Header_t ht3_header_begin;
    fread(&ht3_header_begin, 1, sizeof(ht3_header_begin), fpin);
    // Versions 1.0 (HHT3v1 / PicoHarp) and 2.0 (HHT3v2) are supported;
    // warn only for genuinely unknown format versions.
    if((strncmp(ht3_header_begin.FormatVersion, "1.0", 3) != 0) &&
       (strncmp(ht3_header_begin.FormatVersion, "2.0", 3) != 0)){
        std::cerr << "WARNING: Unknown HT3 format version '"
                  << std::string(ht3_header_begin.FormatVersion, 3)
                  << "' - only versions 1.0 and 2.0 are supported." << std::endl;
    }
    add_tag(data, "Ident", ht3_header_begin.Ident);
    add_tag(data, "FormatVersion", ht3_header_begin.FormatVersion);
    add_tag(data, "CreatorName", ht3_header_begin.CreatorName);
    add_tag(data, "CreatorVersion", ht3_header_begin.CreatorVersion);
    add_tag(data, "FileTime", ht3_header_begin.FileTime);
    add_tag(data, "Comment", ht3_header_begin.CommentField);
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
    add_tag(data, "StopOnOvfl", (bool) ht3_header_begin.StopOnOvfl, tyBool8);
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
if (is_verbose()) {
    std::clog << "FormatVersion:-" << get_tag(data, "FormatVersion")["value"] << "-" << std::endl;
}
    if (get_tag(data, "Ident")["value"] == "HydraHarp") {
        if(get_tag(data, "FormatVersion")["value"] == "1.0"){
if (is_verbose()) {
            std::clog << "Record reader:" << "PQ_RECORD_TYPE_HHT3v1" << std::endl;
}
            add_tag(data, TTTRRecordType, (int) PQ_RECORD_TYPE_HHT3v1, tyInt8);
        } else{
if (is_verbose()) {
            std::clog << "Record reader:" << "PQ_RECORD_TYPE_HHT3v2" << std::endl;
}
            add_tag(data, TTTRRecordType, (int) PQ_RECORD_TYPE_HHT3v2, tyInt8);
        }
    } else {
if (is_verbose()) {
        std::clog << "Record reader:" << "PQ_RECORD_TYPE_PHT3" << std::endl;
}
        add_tag(data, TTTRRecordType, (int) PQ_RECORD_TYPE_PHT3, tyInt8);
    }
    // Effective number of micro time channels
    // TODO: divide by binning factor
    add_tag(data, TTTRNMicroTimes, (int) 32768 / std::max(1, ht3_header_begin.Binning), tyInt8);
    //return 880; // guessed by inspecting several ht3 files
    return static_cast<size_t>(ftell64(fpin));
}

void write_ptu_header(std::string fn, nlohmann::json &data, std::string modes){
    if (is_verbose()) {
    std::clog << "TTTRHeader::write_ptu_header" << std::endl;
}
    // Check for existing file
    // if(boost::filesystem::exists(fn)){
    //     std::clog << "WARNING: File exists" << fn << "." << std::endl;
    // }
    std::ifstream f(fn);
    if(f.good()){
        std::clog << "WARNING: File exists" << fn << "." << std::endl;
    }

    // write header information that is not in header tags
    FILE* fp = fopen(fn.c_str(), modes.c_str());
    // Write identifier for PTU files
    char version[8]; std::string version_str;
    char Magic[8] = "PQTTTR";
    fwrite(&Magic, 1, sizeof(Magic), fp);
    try {
        // A "Tag Version" written by add_tag/set_string_tag lives in the tag
        // list; the PTU reader stores it as a top-level json key. Prefer the
        // tag-list value so programmatically built headers are honoured.
        int idx = find_tag(data, "Tag Version");
        if (idx >= 0)
            version_str = get_tag(data, "Tag Version")["value"];
        else
            version_str = data["Tag Version"];
    } catch (...) {
        std::clog << "WARNING: No PTU version defined in header using default" << std::endl;
        version_str = "0      ";
    }
    strcpy(version, version_str.c_str());
    fwrite(&version, sizeof(version), 1, fp);
    // write header tags
    // variables for writing
    double tmp_d;
    uint64_t tmp_i;
    uint64_t tmp_s;
    std::string tmp_str;
    std::wstring tmp_wstr;
    // Flag to check if the header end tag was written
    bool header_end_written = false;
    for(auto &it: data["tags"].items()){
        auto tag = it.value();
if (is_verbose()) {
        std::clog << tag << std::endl;
}
        tag_head_t TagHead;
        tmp_str.clear();
        tmp_str = tag["name"];
        memset(TagHead.Ident, 0, 32);
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
                tmp_str.resize(tmp_str.length() + tmp_str.length() % 32);
                TagHead.TagValue = tmp_str.length();
                fwrite(&TagHead, sizeof(TagHead), 1, fp);
                fwrite(tmp_str.c_str(), 1, TagHead.TagValue, fp);
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
if (is_verbose()) {
        std::clog << "Header_End is missing. Adding Header_End to tag list." << std::endl;
}
        tag_head_t TagHead;
        TagHead.TagValue = 0;
        strcpy(TagHead.Ident, FileTagEnd.c_str());
        TagHead.Idx = -1;
        fwrite(&TagHead, sizeof(TagHead), 1, fp);
    }
    fclose(fp);
}

void write_ht3_header(std::string fn, nlohmann::json &data, std::string modes){
if (is_verbose()) {
    std::clog << "-- WRITE_HT3_HEADER" << std::endl;
}
    nlohmann::json &json = data;

    // Tag lookup helpers with defaults (get_tag returns a NONE tag when a
    // tag is missing, e.g. when transcoding from another container)
    auto tag_int = [&json](const std::string &name, int32_t d, int idx = -1) -> int32_t {
        if (find_tag(json, name, idx) < 0) return d;
        auto v = get_tag(json, name, idx)["value"];
        if (v.is_boolean()) return (int32_t) v.get<bool>();
        if (v.is_number()) return (int32_t) v.get<double>();
        return d;
    };
    auto tag_double = [&json](const std::string &name, double d) -> double {
        if (find_tag(json, name) < 0) return d;
        auto v = get_tag(json, name)["value"];
        return v.is_number() ? v.get<double>() : d;
    };
    auto tag_string = [&json](const std::string &name, const std::string &d) -> std::string {
        if (find_tag(json, name) < 0) return d;
        auto v = get_tag(json, name)["value"];
        return v.is_string() ? v.get<std::string>() : d;
    };
    auto copy_str = [](char* dst, size_t dst_size, const std::string &src) {
        std::memset(dst, 0, dst_size);
        std::strncpy(dst, src.c_str(), dst_size - 1);
    };

    // Ident and FormatVersion are dictated by the record type actually being
    // written, NOT inherited from the source header. The reader selects
    // HHT3v1/HHT3v2/PHT3 from these two fields, so a stale value silently
    // mislabels the file.
    //
    // This was a real corruption, not a theoretical one. Transcoding an
    // SF-compressed source to plain HHT3v2 kept the source's "1.0", so the
    // reader chose HHT3v1 and then ran SF detection -- and an HHT3v2 overflow
    // record, which legitimately carries a count, looks exactly like an SF one.
    // Every macro time after the first overflow came back multiplied. The
    // event count matched, which is what made it worth guarding against.
    int record_type = (int) data[TTTRRecordType];
    std::string required_ident = "HydraHarp";
    std::string required_version = "2.0";
    if (record_type == PQ_RECORD_TYPE_HHT3v1 ||
        record_type == PQ_RECORD_TYPE_SF_HT3) {
        // SF-compressed files keep the HydraHarp v1 header; the SF record
        // stream is detected from the overflow record payloads on reading.
        required_version = "1.0";
    } else if (record_type == PQ_RECORD_TYPE_PHT3) {
        required_ident = "PicoHarp 300";
    }

    pq_ht3_Header_t ht3_header;
    std::memset(&ht3_header, 0, sizeof(ht3_header));
    copy_str(ht3_header.Ident, sizeof(ht3_header.Ident), required_ident);
    copy_str(ht3_header.FormatVersion, sizeof(ht3_header.FormatVersion), required_version);
    copy_str(ht3_header.CreatorName, sizeof(ht3_header.CreatorName), tag_string("CreatorName", "tttrlib"));
    copy_str(ht3_header.CreatorVersion, sizeof(ht3_header.CreatorVersion), tag_string("CreatorVersion", ""));
    copy_str(ht3_header.FileTime, sizeof(ht3_header.FileTime), tag_string("FileTime", ""));
    ht3_header.CRLF[0] = '\r'; ht3_header.CRLF[1] = '\n';
    copy_str(ht3_header.CommentField, sizeof(ht3_header.CommentField), tag_string("Comment", ""));

    ht3_header.NumberOfCurves = tag_int("NumberOfCurves", 0);
    ht3_header.BitsPerRecord = tag_int(TTTRTagBits, 32);
    ht3_header.ActiveCurve = tag_int("ActiveCurve", 0);
    ht3_header.MeasurementMode = tag_int("MeasurementMode", 3);
    ht3_header.SubMode = tag_int("SubMode", 0);
    // The reader reconstructs the number of micro time channels as
    // 32768 / Binning; derive a default Binning from the number of micro
    // time channels when the Binning tag is absent.
    int n_micro = tag_int(TTTRNMicroTimes, 32768);
    int default_binning = n_micro > 0 ? std::max(1, 32768 / n_micro) : 1;
    ht3_header.Binning = tag_int("Binning", default_binning);
    // Resolution is stored in ps; TTTRTagRes is in seconds
    ht3_header.Resolution = tag_double("Resolution", tag_double(TTTRTagRes, 1e-12) * 1e12);
    ht3_header.Offset = tag_int("Offset", 0);
    ht3_header.AquisitionTime = tag_int("AquisitionTime", 0);
    ht3_header.StopAt = (uint32_t) tag_int("StopAt", 0);
    ht3_header.StopOnOvfl = tag_int("StopOnOvfl", 0);
    ht3_header.Restart = tag_int("Restart", 0);
    ht3_header.DispLinLog = tag_int("DispLinLog", 0);
    ht3_header.DispTimeFrom = tag_int("DispTimeFrom", 0);
    ht3_header.DispTimeTo = tag_int("DispTimeTo", 0);
    ht3_header.DispCountsFrom = tag_int("DispCountsFrom", 0);
    ht3_header.DispCountsTo = tag_int("DispCountsTo", 0);

    // Channel headers: count the per-channel tags written by the reader
    int n_channels = 0;
    while (find_tag(json, "InputRate", n_channels) >= 0) n_channels++;
    ht3_header.InpChansPresent = n_channels;

    // TT mode header; the record count is derived from the file size on
    // reading, nRecords is informational.
    // The macro time calibration of HT3 files is carried by SyncRate
    // (resolution = 1 / SyncRate); when transcoding from a container that
    // stores the global resolution as a tag, derive SyncRate from it so the
    // calibration survives the conversion.
    int default_sync_rate = 0;
    double glob_res = tag_double(TTTRTagGlobRes, -1.0);
    if (glob_res > 0) {
        default_sync_rate = (int) std::llround(1.0 / glob_res);
    }
    pq_ht3_TTModeHeader_t tt_mode_hdr;
    std::memset(&tt_mode_hdr, 0, sizeof(tt_mode_hdr));
    tt_mode_hdr.SyncRate = tag_int("SyncRate", default_sync_rate);
    tt_mode_hdr.StopAfter = tag_int("StopAfter", 0);
    tt_mode_hdr.StopReason = tag_int("StopReason", 0);
    tt_mode_hdr.nRecords = (uint64_t) tag_int("nRecords", 0);

    // Imaging header blob (marker/scan configuration for CLSM files)
    std::vector<int32_t> img_hdr;
    if (find_tag(json, "ImgHdr") >= 0) {
        auto v = get_tag(json, "ImgHdr")["value"];
        if (v.is_array()) img_hdr = v.get<std::vector<int32_t>>();
    }
    tt_mode_hdr.ImgHdrSize = (int32_t) img_hdr.size();

    FILE* fp = fopen(fn.c_str(), modes.c_str());
    if (fp == nullptr) {
        std::cerr << "ERROR: Cannot write HT3 header to file: " << fn << std::endl;
        return;
    }
    fwrite(&ht3_header, sizeof(ht3_header), 1, fp);
    pq_ht3_ChannelHeader_t channel_header;
    for (int i = 0; i < n_channels; i++) {
        std::memset(&channel_header, 0, sizeof(channel_header));
        channel_header.InputCFDLevel = tag_int("InputCFDLevel", 0, i);
        channel_header.InputCFDZeroCross = tag_int("InputCFDZeroCross", 0, i);
        channel_header.InputOffset = tag_int("InputOffset", 0, i);
        channel_header.InputRate = tag_int("InputRate", 0, i);
        fwrite(&channel_header, sizeof(channel_header), 1, fp);
    }
    fwrite(&tt_mode_hdr, sizeof(tt_mode_hdr), 1, fp);
    if (!img_hdr.empty()) {
        fwrite(img_hdr.data(), sizeof(int32_t), img_hdr.size(), fp);
    }
    fclose(fp);
}

}  // namespace io
}  // namespace tttrlib
