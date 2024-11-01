#include "TTTR.h"
#include "TTTRRange.h"
#include "TTTRHeader.h"

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif


TTTRHeader::TTTRHeader() :
        header_end(0)
{
    json_data = nlohmann::json::object();
    json_data["tags"] = nlohmann::json::array();
    json_data[TTTRContainerType] = 0;
    json_data[TTTRRecordType] = -1;
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- TTTRHeader::TTTRHeader" << std::endl;
#endif
}

TTTRHeader::TTTRHeader(const TTTRHeader &p2)
{
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- TTTRHeader::TTTRHeader - Copy constructor" << std::endl;
#endif
    json_data = p2.json_data;
    header_end = p2.header_end;
}

TTTRHeader::TTTRHeader(
        std::FILE *fpin,
        int tttr_container_type,
        bool close_file
        ) : TTTRHeader(tttr_container_type)
{
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- TTTRHeader::TTTRHeader - Opening file" << std::endl;
    std::clog << "reading header" << std::endl;
#endif
    int tttr_record_type;
    if(tttr_container_type == PQ_PTU_CONTAINER){
        header_end = read_ptu_header(fpin, tttr_record_type, json_data);
        int RecordType = get_tag(json_data, "TTResultFormat_TTTRRecType")["value"];
        switch (RecordType)
        {
            case rtPicoHarpT2:
                tttr_record_type = PQ_RECORD_TYPE_PHT2;
                break;
            case rtPicoHarpT3:
                tttr_record_type = PQ_RECORD_TYPE_PHT3;
                break;
            case rtHydraHarpT2:
                tttr_record_type = PQ_RECORD_TYPE_HHT2v1;
                break;
            case rtHydraHarpT3:
                tttr_record_type = PQ_RECORD_TYPE_HHT3v1;
                break;
            case rtMultiHarpT2:
            case rtHydraHarp2T2:
            case rtTimeHarp260NT2:
            case rtTimeHarp260PT2:
                tttr_record_type = PQ_RECORD_TYPE_HHT2v2;
                break;
            case rtMultiHarpT3:
            case rtHydraHarp2T3:
            case rtTimeHarp260NT3:
            case rtTimeHarp260PT3:
                tttr_record_type = PQ_RECORD_TYPE_HHT3v2;
                break;
            default:
                tttr_record_type = PQ_RECORD_TYPE_HHT3v2;
        }
    } else if(tttr_container_type == CZ_CONFOCOR3_CONTAINER) {
        header_end = read_cz_confocor3_header(fpin, json_data);
        tttr_record_type = get_tag(json_data, TTTRRecordType)["value"];
    } else if(tttr_container_type == SM_CONTAINER){
        header_end = read_sm_header(fpin, json_data);
        tttr_record_type = get_tag(json_data, TTTRRecordType)["value"];
    }
    else if(tttr_container_type == PQ_HT3_CONTAINER){
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
        tttr_record_type = BH_RECORD_TYPE_SPC130;
    } else{
        header_end = 0;
        add_tag(json_data, TTTRTagBits, 32, tyInt8);
        tttr_record_type = BH_RECORD_TYPE_SPC130;
    }
    set_tttr_record_type(tttr_record_type);
    if(close_file) fclose(fpin);
#ifdef VERBOSE_TTTRLIB
    std::clog << "End of header: " << header_end << std::endl;
    std::clog << json_data << std::endl;
#endif
}


TTTRHeader::TTTRHeader(
        std::string fn,
        int tttr_container_type
) : TTTRHeader(fopen(fn.c_str(), "r"), tttr_container_type, true) {

}


TTTRHeader::TTTRHeader(int tttr_container_type) : TTTRHeader(){
    set_tttr_container_type(tttr_container_type);
};


size_t TTTRHeader::read_bh132_header(
        std::FILE *fpin,
        nlohmann::json &data,
        bool rewind
){
    if(rewind) std::fseek(fpin, 0, SEEK_SET);
    bh_spc132_header_t rec;
    fread(&rec, sizeof(rec),1, fpin);
    double mt_clk = (double) rec.bits.macro_time_clock / 10.0e9; // divide by 10.0e9 to get units of seconds
    double mi_clk = mt_clk / 4096.0;
    add_tag(data, TTTRTagRes, mi_clk, tyFloat8);
    add_tag(data, TTTRTagGlobRes, mt_clk, tyFloat8);
    add_tag(data, TTTRNMicroTimes, 4096, tyInt8);
    add_tag(data, TTTRTagBits, 32, tyInt8);

#ifdef VERBOSE_TTTRLIB
    std::clog << "-- BH132 header reader " << std::endl;
    std::clog << "-- macro_time_resolution: " << mt_clk << std::endl;
    std::clog << "-- micro_time_resolution: " << mi_clk << std::endl;
#endif
    return 4;
}


size_t TTTRHeader::read_sm_header(FILE* file, nlohmann::json &j) {

    add_tag(j, TTTRRecordType, (int) SM_RECORD_TYPE, tyInt8);

    // Helper lambda to read and swap endianness
    auto read_and_swap = [&](auto& value) {
        fread(&value, sizeof(value), 1, file);
        SwapEndian(value);
    };

    // Helper lambda to read a string with its size
    auto read_string = [&](const std::string& tag_name) {
        uint32_t size;
        read_and_swap(size);
        char* buffer = new char[size];
        fread(buffer, sizeof(char), size, file);
        add_tag(j, tag_name, buffer, tyAnsiString);
        delete[] buffer;
    };
    sm_header_t header;  // Use only the 'header' structure

    // Read and swap the version
    read_and_swap(header.version);
    add_tag(j, "version", (int) header.version, tyInt8);

    read_string("comment");
    read_string("simple");

    read_and_swap(header.pointer1);
    add_tag(j, "pointer1", (int) header.pointer1, tyInt8);

    read_string("file_section_type");

    read_and_swap(header.magic1);
    add_tag(j, "magic1", (int) header.magic1, tyInt8);
    read_and_swap(header.magic2);
    add_tag(j, "magic2", (int) header.magic2, tyInt8);

    read_string("col1_name");
    read_and_swap(header.col1_resolution);
    add_tag(j, "col1_resolution", (double) header.col1_resolution, tyFloat8);
    read_and_swap(header.col1_offset);
    add_tag(j, "col1_offset", (double) header.col1_offset, tyFloat8);
    read_and_swap(header.col1_bho);
    add_tag(j, "col1_bho", (int) header.col1_bho, tyInt8);

    // Read the column 2 information
    read_string("col2_name");
    read_and_swap(header.col2_resolution);
    add_tag(j, "col2_resolution", (double) header.col2_resolution, tyFloat8);
    read_and_swap(header.col2_offset);
    add_tag(j, "col2_offset", (double) header.col2_offset, tyFloat8);
    read_and_swap(header.col2_bho);
    add_tag(j, "col2_bho", (int) header.col2_bho, tyInt8);

    read_string("col3_name");
    read_and_swap(header.col3_resolution);
    add_tag(j, "col3_resolution", (double) header.col3_resolution, tyFloat8);
    read_and_swap(header.col3_offset);
    add_tag(j, "col3_offset", (double) header.col3_offset, tyFloat8);

    // Read the number of channels
    int32_t num_channels;
    read_and_swap(num_channels);
    add_tag(j, "num_channels", (int) num_channels, tyInt8);

    header.channel_labels.resize(num_channels);
    for (uint32_t i = 0; i < num_channels; ++i) {
        uint32_t size;
        fread(&size, sizeof(size), 1, file);
        SwapEndian(size);
        fseek(file, size, SEEK_CUR);
    }

    add_tag(j, TTTRTagGlobRes, (double) header.col2_resolution, tyFloat8);

    // Return the current file position, which is the cursor
    return ftell(file);
}



size_t TTTRHeader::read_cz_confocor3_header(
        std::FILE *fpin,
        nlohmann::json &data,
        bool rewind
) {
    if(rewind) std::fseek(fpin, 0, SEEK_SET);
    cz_confocor3_settings_t rec;
    fread(&rec, sizeof(rec),1, fpin);

    float frequency_float = rec.bits.frequency;
    double mt_clk = 1. / frequency_float;

    // Convert each element to hexadecimal and concatenate them
    std::stringstream ss;
    for (int i = 0; i < 4; i++) {
        ss << std::hex << std::setw(8) << std::setfill('0') << rec.bits.measure_id[i];
    }
    size_t total_length = ss.str().length() + 1;
    char* hex_measure_id = new char[total_length];
    std::strcpy(hex_measure_id, ss.str().c_str());

    int measurement_position = rec.bits.measurement_position;
    int kinetic_index = rec.bits.kinetic_index;
    int repetition_number = rec.bits.repetition_number;
    int channel_nbr = rec.bits.channel - 48;

    add_tag(data, TTTRTagGlobRes, mt_clk, tyFloat8);
    // Convert ASCII channel number to int
    add_tag(data, TTTRRecordType, (int) CZ_RECORD_TYPE_CONFOCOR3, tyInt8);
    add_tag(data, "channel", channel_nbr, tyInt8);
    add_tag(data, "measure_id", hex_measure_id, tyAnsiString);
    add_tag(data, "measurement_position", measurement_position + 1, tyInt8);
    add_tag(data, "kinetic_index", kinetic_index + 1, tyInt8);
    add_tag(data, "repetition_number", repetition_number + 1, tyInt8);
    add_tag(data, TTTRTagBits, 32, tyInt8);
#ifdef VERBOSE_TTTRLIB
    std::clog << "-- Confocor3 header reader " << std::endl;
    std::clog << "-- frequency_float: " << frequency_float << std::endl;
    std::clog << "-- measure_id_string: " << hex_measure_id << std::endl;
    std::clog << "-- macro_time_resolution: " << mt_clk << std::endl;
    std::clog << "-- channel_nbr: " << channel_nbr << std::endl;
    std::clog << "-- measurement_position: " << measurement_position << std::endl;
    std::clog << "-- kinetic_index: " << kinetic_index << std::endl;
    std::clog << "-- repetition_number: " << repetition_number << std::endl;
    std::clog << "-- header bytes: " << sizeof(rec) << std::endl;
#endif
    return (size_t) ftell(fpin);
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
#ifdef VERBOSE_TTTRLIB
    std::clog << "FormatVersion:-" << get_tag(data, "FormatVersion")["value"] << "-" << std::endl;
#endif
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
#ifdef VERBOSE_TTTRLIB
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

#ifdef VERBOSE_TTTRLIB
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
#ifdef VERBOSE_TTTRLIB
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
                    add_tag(json_data, key,AnsiBuffer, TagHead.Typ, TagHead.Idx);
                    free(AnsiBuffer);
                    break;
                case tyWideString:
                      WideBuffer = (wchar_t *) calloc((size_t) TagHead.TagValue, 1);
                      Result = fread(WideBuffer, 1, (size_t) TagHead.TagValue, fpin);
                      std::cerr << "ERROR: reading of tyWideString currently not supported" << std::endl;
                      if (Result != TagHead.TagValue) {
                          free(WideBuffer);
                          throw std::string("Incomplete File");
                      }
                      add_tag(json_data, key, WideBuffer, TagHead.Typ, TagHead.Idx);
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
        case rtMultiHarpT2:
        case rtHydraHarp2T2:
        case rtTimeHarp260NT2:
        case rtTimeHarp260PT2:
            tttr_record_type = PQ_RECORD_TYPE_HHT2v2;
            break;
        case rtHydraHarpT3:
        case rtMultiHarpT3:
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
        std::string fn, TTTRHeader* header, std::string mode){
    // write header
    bh_spc132_header_t head;
    head.allbits = 0;
    head.bits.unused = 0;
    head.bits.invalid = true;

    nlohmann::json tag = get_tag(header->json_data, TTTRTagGlobRes);
    head.bits.macro_time_clock = (unsigned) ((double) tag["value"] * 10.e9);

    FILE* fp = fopen(fn.c_str(), mode.c_str());
    fwrite(&head, 4, 1, fp);
    fclose(fp);
}


void TTTRHeader::write_ptu_header(std::string fn, TTTRHeader* header, std::string modes){
    #ifdef VERBOSE_TTTRLIB
    std::clog << "TTTRHeader::write_ptu_header" << std::endl;
    #endif
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
        version_str = header->json_data["Tag Version"];
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
    for(auto &it: header->json_data["tags"].items()){
        auto tag = it.value();
#ifdef VERBOSE_TTTRLIB
        std::clog << tag << std::endl;
#endif
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
#ifdef VERBOSE_TTTRLIB
        std::clog << "Header_End is missing. Adding Header_End to tag list." << std::endl;
#endif
        tag_head_t TagHead;
        TagHead.TagValue = 0;
        strcpy(TagHead.Ident, FileTagEnd.c_str());
        TagHead.Idx = -1;
        fwrite(&TagHead, sizeof(TagHead), 1, fp);
    }
    fclose(fp);
}

int TTTRHeader::find_tag(
        nlohmann::json &json_data,
        const std::string &name,
        int idx
) {
    int tag_idx = -1;
    int curr_idx = 0;
    for (auto &it : json_data["tags"].items()) {
        if ((it.value()["name"] == name) && (it.value()["idx"] == idx)) {
            tag_idx = curr_idx;
            break;
        }
        curr_idx++;
    }
#ifdef VERBOSE_TTTRLIB
    std::clog << "FIND_TAG: " << name << ":" << idx << ":" << tag_idx  << std::endl;
#endif
    return tag_idx;
}


void TTTRHeader::write_ht3_header(std::string fn, TTTRHeader* header, std::string modes){
//#ifdef VERBOSE_TTTRLIB
//    std::clog << "-- WRITE_HT3_HEADER" << std::endl;
//#endif
//    if(boost::filesystem::exists(fn)){
//        std::clog << "WARNING: File exists" << fn << "." << std::endl;
//    }
//    auto json = header->json_data;
//    std::string s;
//    FILE* fp = fopen(fn.c_str(), modes.c_str());
//    // Header of HT3 file
//    pq_ht3_Header_t ht3_header_begin;
//    strcpy(ht3_header_begin.FormatVersion, "1.0");
//
//    //
//    s = TTTRHeader::get_tag("Ident", json)["value"];
//    strcpy(ht3_header_begin.Ident, s.c_str());
//    //
//    s = TTTRHeader::get_tag("FormatVersion", json)["value"];
//    strcpy(ht3_header_begin.FormatVersion, s.c_str());
//    //
//    s = TTTRHeader::get_tag("CreatorName", json)["value"];
//    strcpy(ht3_header_begin.CreatorName, s.c_str());
//    //
//    s = TTTRHeader::get_tag("CreatorVersion", json)["value"];
//    strcpy(ht3_header_begin.CreatorVersion, s.c_str());
//    //
//    s = TTTRHeader::get_tag("FileTime", json)["value"];
//    strcpy(ht3_header_begin.FileTime, s.c_str());
//    //
//    s = TTTRHeader::get_tag("Comment", json)["value"];
//    strcpy(ht3_header_begin.CommentField, s.c_str());
//    //
//    ht3_header_begin.NumberOfCurves = TTTRHeader::get_tag("NumberOfCurves", json)["value"];
//    ht3_header_begin.BitsPerRecord = TTTRHeader::get_tag(TTTRTagBits, json)["value"];
//    ht3_header_begin.ActiveCurve = TTTRHeader::get_tag("ActiveCurve", json)["value"];
//    ht3_header_begin.MeasurementMode = TTTRHeader::get_tag("MeasurementMode", json)["value"];
//    ht3_header_begin.SubMode = TTTRHeader::get_tag("SubMode", json)["value"];
//    ht3_header_begin.Binning = TTTRHeader::get_tag("Binning", json)["value"];
//    ht3_header_begin.Resolution = TTTRHeader::get_tag("Resolution", json)["value"];
//    ht3_header_begin.Offset = TTTRHeader::get_tag("Offset", json)["value"];
//    ht3_header_begin.AquisitionTime = TTTRHeader::get_tag("AquisitionTime", json)["value"];
//    ht3_header_begin.StopAt = TTTRHeader::get_tag("StopAt", json)["value"];
//    ht3_header_begin.StopOnOvfl = TTTRHeader::get_tag("StopOnOvfl", json)["value"];
//    ht3_header_begin.Restart = TTTRHeader::get_tag("Restart", json)["value"];
//    ht3_header_begin.DispLinLog = TTTRHeader::get_tag("DispLinLog", json)["value"];
//    ht3_header_begin.DispTimeFrom = TTTRHeader::get_tag("DispTimeFrom", json)["value"];
//    ht3_header_begin.DispTimeTo = TTTRHeader::get_tag("DispTimeTo", json)["value"];
//    ht3_header_begin.DispCountsFrom = TTTRHeader::get_tag("DispCountsFrom", json)["value"];
//    ht3_header_begin.DispCountsTo = TTTRHeader::get_tag("DispCountsTo", json)["value"];
//
//    std::vector<pq_ht3_ChannelHeader_t> channel_settings;
//    channel_settings.resize(ht3_header_begin.InpChansPresent);
//    for(int i=0; i<ht3_header_begin.InpChansPresent; i++){
//        channel_settings[i].InputCFDLevel = TTTRHeader::get_tag("InputCFDLevel", json, i)["value"];
//        channel_settings[i].InputCFDZeroCross = TTTRHeader::get_tag("InputCFDZeroCross", json, i)["value"];
//        channel_settings[i].InputOffset = TTTRHeader::get_tag("InputOffset", json, i)["value"];
//        channel_settings[i].InputRate = TTTRHeader::get_tag("InputRate", json, i)["value"];
//    }
//
//    // pq_ht3_TTModeHeader_t
//    pq_ht3_TTModeHeader_t tt_mode_hdr;
//    fread(&tt_mode_hdr, 1, sizeof(tt_mode_hdr), fpin);
//    add_tag(data, "SyncRate", tt_mode_hdr.SyncRate, tyInt8);
//    add_tag(data, "StopAfter", tt_mode_hdr.StopAfter, tyInt8);
//    add_tag(data, "StopReason", tt_mode_hdr.StopReason, tyInt8);
//    add_tag(data, "ImgHdrSize", tt_mode_hdr.ImgHdrSize, tyInt8);
//    add_tag(data, "nRecords", (int) tt_mode_hdr.nRecords, tyInt8);
//
//    // ImgHdr
////    fseek(fpin, (long) tt_mode_hdr.ImgHdrSize, SEEK_CUR);
//    int ImgHdrSize = tt_mode_hdr.ImgHdrSize;
//    if(ImgHdrSize > 0){
//        auto imgHdr_array = (int32_t*) calloc(ImgHdrSize, sizeof(int32_t));
//        fread(imgHdr_array, sizeof(int32_t), ImgHdrSize, fpin);
//        std::vector<int32_t> v;
//        for (int i=0; i<ImgHdrSize; i++) {
//            v.emplace_back(imgHdr_array[i]);
//        };
//        free(imgHdr_array);
//        add_tag(data, "ImgHdr", v, tyBinaryBlob);
//        add_tag(data, "ImgHdr", v, tyBinaryBlob);
//
//        add_tag(data, "ImgHdr_Frame", v[2] + 1, tyInt8);
//        add_tag(data, "ImgHdr_LineStart", v[3], tyInt8);
//        add_tag(data, "ImgHdr_LineStop", v[4], tyInt8);
//        add_tag(data, "ImgHdr_PixX", v[6], tyInt8);
//        add_tag(data, "ImgHdr_PixY", v[7], tyInt8);
//    }
//
//    double resolution = std::max(1.0, ht3_header_begin.Resolution) * 1e-12;
//    add_tag(data, TTTRTagRes, resolution, tyFloat8);
//
//    //return 880; // guessed by inspecting several ht3 files
//    return (size_t) ftell(fpin);
}


void TTTRHeader::add_tag(
        nlohmann::json &json_data,
        const std::string &name,
        std::any value,
        // boost::any value,
        unsigned int type,
        int idx
) {
    using namespace std;
    nlohmann::json tag;
    tag["name"] = name; // boost::locale::conv::to_utf<char>(name,"ISO-8859-1"); // there are sometimes conversion issues
    tag["type"] = type;
    tag["idx"] = idx;
    if (type == tyEmpty8) {
        tag["value"] = nullptr;
    } else if (type == tyBool8) {
        tag["value"] = any_cast<bool>(value);
    } else if ((type == tyInt8) || (type == tyBitSet64) || (type == tyColor8)) {
        tag["value"] = any_cast<int>(value);
    } else if ((type == tyFloat8) || (type == tyTDateTime)) {
        tag["value"] = any_cast<double>(value);
    } else if (type == tyFloat8Array) {
        tag["value"] = any_cast<std::vector<double>>(value);
    }
    else if (type == tyAnsiString) {
        auto str = any_cast<char*>(value);
        tag["value"] = str;
        // the stuff below work better but depnds on boost
        // auto str2 = std::string(str);
        // auto str3 = boost::locale::conv::to_utf<char>(str2,"ISO-8859-1");
        // tag["value"] = str3;
    }
    else if (type == tyWideString) {
        auto str = any_cast<wchar_t *>(value);
        auto str2 = std::wstring(str);
        tag["value"] = str2;
    }
    else if (type == tyBinaryBlob) {
        tag["value"] = any_cast<std::vector<int32_t>>(value);
    } else {
        tag["value"] = std::to_string(any_cast<int>(value));
    }
    int tag_idx = find_tag(json_data, name, idx);
    if (tag_idx < 0) {
        json_data["tags"].emplace_back(tag);
    } else {
        json_data["tags"][tag_idx] = tag;
    }
#ifdef VERBOSE_TTTRLIB
    std::clog << "ADD_TAG: " << tag << std::endl;
#endif
}


nlohmann::json TTTRHeader::get_tag(
        nlohmann::json json_data,
        const std::string &name,
        int idx
){
    for (auto& it : json_data["tags"].items()) {
        if(it.value()["name"] == name){
            if((idx < 0) || (idx == it.value()["idx"])){
#ifdef VERBOSE_TTTRLIB
                std::clog << "-- GET_TAG:" << name << ":" << it << std::endl;
#endif
                return it.value();
            }
        }
    }
#ifdef VERBOSE_TTTRLIB
    std::cerr << "ERROR: TTTR-TAG " << name << ":" << idx << " not found." << std::endl;
#endif
    nlohmann::json re = {
            {"value", -1.0},
            {"idx", -1},
            {"name", "NONE"}
    };
    return re;
}


double TTTRHeader::get_macro_time_resolution(){
    double res;
    auto tag = get_tag(json_data, TTTRTagGlobRes);
    if(tag["name"] == "NONE"){
        res = 1. / (double) get_tag(json_data, TTTRSyncRate)["value"];
    } else{
        res = tag["value"];
    }
    return res;
}


std::string TTTRHeader::get_json(std::string tag_name, int idx, int indent){
    std::string s;
    if(tag_name.empty()){
        s = json_data.dump(indent);
    } else{
        int tag_idx = find_tag(json_data, tag_name, idx);
        if(tag_idx >= 0){
            s = json_data["tags"][tag_idx].dump(indent);
        } else {
            s = "{}";
        }
    }
    return s;
}

