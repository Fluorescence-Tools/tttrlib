// SPDX-License-Identifier: BSD-3-Clause
#include "TTTR.h"
#include "TTTRRange.h"
#include "TTTRHeader.h"
#include "TTTRTags.h"
#include "FileCheck.h"
#include "Verbose.h"

#ifdef BUILD_PHOTON_HDF
#include <highfive/H5File.hpp>
#include <highfive/H5Group.hpp>
#include <highfive/H5DataSet.hpp>
#include <highfive/H5DataType.hpp>
#endif

#include <nlohmann/json.hpp>

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif



TTTRHeader::TTTRHeader() :
        json_data_(new nlohmann::json()),
        header_end(0)
{
    json_data() = nlohmann::json::object();
    json_data()["tags"] = nlohmann::json::array();
    json_data()[TTTRContainerType] = 0;
    json_data()[TTTRRecordType] = -1;
if (is_verbose()) {
    std::clog << "-- TTTRHeader::TTTRHeader" << std::endl;
}
}

TTTRHeader::TTTRHeader(const TTTRHeader &p2) :
        json_data_(new nlohmann::json())
{
if (is_verbose()) {
    std::clog << "-- TTTRHeader::TTTRHeader - Copy constructor" << std::endl;
}
    json_data() = p2.json_data();
    header_end = p2.header_end;
}

TTTRHeader::TTTRHeader(
        std::FILE *fpin,
        int tttr_container_type,
        bool close_file
        ) : TTTRHeader(tttr_container_type)
{
if (is_verbose()) {
    std::clog << "-- TTTRHeader::TTTRHeader - Opening file" << std::endl;
    std::clog << "reading header" << std::endl;
}
    int tttr_record_type;
    if(tttr_container_type == PQ_PTU_CONTAINER){
        header_end = read_ptu_header(fpin, tttr_record_type, json_data());
        int RecordType = get_tag(json_data(), "TTResultFormat_TTTRRecType")["value"];
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
            case rtHydraHarp2T2:
            case rtTimeHarp260NT2:
            case rtTimeHarp260PT2:
                tttr_record_type = PQ_RECORD_TYPE_HHT2v2;
                break;
            case rtMultiHarpT2:
                tttr_record_type = PQ_RECORD_TYPE_GENERIC_T2;
                break;
            case rtHydraHarp2T3:
            case rtTimeHarp260NT3:
            case rtTimeHarp260PT3:
                tttr_record_type = PQ_RECORD_TYPE_HHT3v2;
                break;
            case rtMultiHarpT3:
                tttr_record_type = PQ_RECORD_TYPE_GENERIC_T3;
                break;
            default:
                tttr_record_type = PQ_RECORD_TYPE_HHT3v2;
        }
    } else if(tttr_container_type == CZ_CONFOCOR3_CONTAINER) {
        header_end = read_cz_confocor3_header(fpin, json_data());
        tttr_record_type = get_tag(json_data(), TTTRRecordType)["value"];
    } else if(tttr_container_type == SM_CONTAINER){
        header_end = read_sm_header(fpin, json_data());
        tttr_record_type = get_tag(json_data(), TTTRRecordType)["value"];
    }
    else if(tttr_container_type == PQ_HT3_CONTAINER){
        header_end = read_ht3_header(fpin, json_data());
        tttr_record_type = get_tag(json_data(), TTTRRecordType)["value"];
    } else if(tttr_container_type == BH_SPC600_256_CONTAINER){
        header_end = 0;
        add_tag(json_data(), TTTRTagGlobRes, 1.0, tyFloat8);
        add_tag(json_data(), TTTRNMicroTimes, 256, tyInt8);
        add_tag(json_data(), TTTRTagBits, 32, tyInt8);
        tttr_record_type = BH_RECORD_TYPE_SPC600_256;
    } else if(tttr_container_type == BH_SPC600_4096_CONTAINER){
        header_end = 0;
        add_tag(json_data(), TTTRTagGlobRes, 1.0, tyFloat8);
        add_tag(json_data(), TTTRNMicroTimes, 4096, tyInt8);
        add_tag(json_data(), TTTRTagBits, 48, tyInt8);
        tttr_record_type = BH_RECORD_TYPE_SPC600_4096;
    } else if(tttr_container_type == BH_SPC130_CONTAINER){
        header_end = read_bh132_header(fpin, json_data());
        tttr_record_type = BH_RECORD_TYPE_SPC130;
    } else if(tttr_container_type == BH_SPCQC_CONTAINER){
        header_end = read_bh_spcqc_header(fpin, json_data());
        // QC-x04 and QC-x06 differ in the channel width; the header picks one
        tttr_record_type = get_tag(json_data(), TTTRRecordType)["value"];
    } else{
        header_end = 0;
        add_tag(json_data(), TTTRTagBits, 32, tyInt8);
        tttr_record_type = BH_RECORD_TYPE_SPC130;
    }
    set_tttr_record_type(tttr_record_type);
    if(close_file) fclose(fpin);
if (is_verbose()) {
    std::clog << "End of header: " << header_end << std::endl;
    std::clog << json_data() << std::endl;
}
}


TTTRHeader::TTTRHeader(
        std::string fn,
        int tttr_container_type
) : TTTRHeader(open_file(fn, "r"), tttr_container_type, true) {

}


TTTRHeader::TTTRHeader(int tttr_container_type) : TTTRHeader(){
    set_tttr_container_type(tttr_container_type);
};


TTTRHeader& TTTRHeader::operator=(const TTTRHeader &p2){
    if (this != &p2) {
        json_data() = p2.json_data();
        header_end = p2.header_end;
    }
    return *this;
}


// Defined here, not in the class body: `json_data_` points at an incomplete
// type in TTTRHeader.h, so unique_ptr's deleter cannot be instantiated there.
TTTRHeader::~TTTRHeader() = default;


nlohmann::json& TTTRHeader::json_data(){
    return *json_data_;
}

const nlohmann::json& TTTRHeader::json_data() const {
    return *json_data_;
}


// ---------------------------------------------------------------------------
// Accessors that used to be inline in TTTRHeader.h. They live here so the
// header needs only <nlohmann/json_fwd.hpp>; none of them is on a per-photon
// path -- they are read once per file.
// ---------------------------------------------------------------------------

int TTTRHeader::get_tttr_record_type(){
    return (int) json_data()[TTTRRecordType];
}

void TTTRHeader::set_tttr_record_type(int v){
    json_data()[TTTRRecordType] = v;
}

int TTTRHeader::get_tttr_container_type(){
    return (int) json_data()[TTTRContainerType];
}

void TTTRHeader::set_tttr_container_type(int v){
    json_data()[TTTRContainerType] = v;
}

size_t TTTRHeader::get_bytes_per_record(){
    return (size_t) get_tag(json_data(), TTTRTagBits)["value"] / 8;
}

size_t TTTRHeader::size(){
    return json_data()["tags"].size();
}

nlohmann::json& TTTRHeader::operator[](std::size_t idx){
    return json_data()["tags"][idx];
}

const nlohmann::json& TTTRHeader::operator[](std::size_t idx) const {
    return json_data()["tags"][idx];
}

unsigned int TTTRHeader::get_number_of_micro_time_channels(){
    int v = get_tag(json_data(), TTTRNMicroTimes)["value"];
    if(v < 0){
        return 0;
    } else{
        return v;
    }
}

double TTTRHeader::get_micro_time_resolution(){
    return get_tag(json_data(), TTTRTagRes)["value"];
}

void TTTRHeader::set_micro_time_resolution(double resolution){
    TTTRHeader::add_tag(json_data(), TTTRTagRes, resolution, tyFloat8, -1);
}

void TTTRHeader::set_macro_time_resolution(double resolution){
    TTTRHeader::add_tag(json_data(), TTTRTagGlobRes, resolution, tyFloat8, -1);
}

void TTTRHeader::set_number_of_micro_time_channels(int n_channels){
    TTTRHeader::add_tag(json_data(), TTTRNMicroTimes, n_channels, tyInt8, -1);
}

void TTTRHeader::set_float_tag(const std::string& name, double value){
    TTTRHeader::add_tag(json_data(), name, value, tyFloat8, -1);
}

void TTTRHeader::set_int_tag(const std::string& name, int value){
    TTTRHeader::add_tag(json_data(), name, value, tyInt8, -1);
}

void TTTRHeader::set_blob_tag(const std::string& name, const std::vector<int32_t>& value){
    TTTRHeader::add_tag(json_data(), name, value, tyBinaryBlob, -1);
}

void TTTRHeader::set_string_tag(const std::string& name, const std::string& value){
    std::string copy = value;
    TTTRHeader::add_tag(json_data(), name, const_cast<char*>(copy.c_str()), tyAnsiString, -1);
}

int TTTRHeader::get_pixel_duration(){
    double pixel_duration_d = 0.0;
    auto tpp = TTTRHeader::get_tag(json_data(), "ImgHdr_TimePerPixel");
    if (!tpp.is_null() && tpp.contains("value") && !tpp["value"].is_null())
        pixel_duration_d = tpp["value"].get<double>();
    else
        pixel_duration_d = TTTRHeader::get_tag(
                json_data(), "$TimePerPixel")["value"];
    double global_res = TTTRHeader::get_tag(
            json_data(), "MeasDesc_GlobalResolution")["value"];
    // Round to nearest integer duration in macro clock units and cast explicitly to int
    int pixel_duration = static_cast<int>(std::llround(pixel_duration_d / global_res));
    return pixel_duration;
}

int TTTRHeader::get_line_duration(){
    double pixel_duration_d = 0.0;
    auto tpp = TTTRHeader::get_tag(json_data(), "ImgHdr_TimePerPixel");
    if (!tpp.is_null() && tpp.contains("value") && !tpp["value"].is_null())
        pixel_duration_d = tpp["value"].get<double>();
    else
        pixel_duration_d = TTTRHeader::get_tag(
                json_data(), "$TimePerPixel")["value"];
    double global_res_d = TTTRHeader::get_tag(
            json_data(), "MeasDesc_GlobalResolution")["value"];
    double n_pixel = TTTRHeader::get_tag(json_data(), "ImgHdr_PixX")["value"];
    int line_duration = static_cast<int>(std::ceil((pixel_duration_d * n_pixel) / global_res_d));
    return line_duration;
}

void TTTRHeader::set_json(std::string json_string){
    json_data() = nlohmann::json::parse(json_string);
}


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

if (is_verbose()) {
    std::clog << "-- BH132 header reader " << std::endl;
    std::clog << "-- macro_time_resolution: " << mt_clk << std::endl;
    std::clog << "-- micro_time_resolution: " << mi_clk << std::endl;
}
    return 4;
}


size_t TTTRHeader::read_bh_spcqc_header(
        std::FILE *fpin,
        nlohmann::json &data,
        bool rewind
){
    if(rewind) std::fseek(fpin, 0, SEEK_SET);
    bh_spcqc_header_t rec;
    fread(&rec, sizeof(rec),1, fpin);

    // The femto flag selects the unit of the 22 bit clock field. Without it the
    // QC modules could not express their clock at all: 2.048131 ns needs
    // femtoseconds, and the classic 0.1 ns unit would round it to 2.0 ns. When
    // the flag is clear the classic unit applies, which is also the only way
    // the field can reach into the microsecond range.
    double mt_clk = (double) rec.bits.macro_time_clock *
                    (rec.bits.femto ? 1e-15 : 1e-10);
    // The TAC of the QC modules is not slaved to the macro time clock, so the
    // micro time resolution cannot be computed from mt_clk. Assume the TAC range
    // SPCM writes by default; read_bh_set_file overrides it from SP_TAC_R /
    // SP_ADC_RE whenever the .set sidecar is available.
    double mi_clk = BH_SPCQC_DEFAULT_TAC_RANGE / (double) BH_SPCQC_N_MICRO_TIMES;
    add_tag(data, TTTRTagRes, mi_clk, tyFloat8);
    add_tag(data, TTTRTagGlobRes, mt_clk, tyFloat8);
    add_tag(data, TTTRNMicroTimes, (int) BH_SPCQC_N_MICRO_TIMES, tyInt8);
    add_tag(data, TTTRTagBits, 32, tyInt8);
    // Keep the flags: the routing width is needed to split a decoded channel
    // back into input channel and router signal on write, and the marker flag
    // records whether the file was written in imaging mode.
    add_tag(data, "BH_SPCQC_RoutingBits", (int) rec.bits.n_routing_bits, tyInt8);
    add_tag(data, "BH_SPCQC_HasMarkers", (int) rec.bits.markers, tyInt8);
    add_tag(data, "BH_SPCQC_FemtoClock", (int) rec.bits.femto, tyInt8);
    // Six input channels widen the channel field into bit 30, which is a record
    // selector in the QC-x04 layout -- the two cannot share a decoder.
    add_tag(data, TTTRRecordType,
            rec.bits.six_channel ? BH_RECORD_TYPE_SPCQC_X06
                                 : BH_RECORD_TYPE_SPCQC_X04, tyInt8);

if (is_verbose()) {
    std::clog << "-- BH SPC-QC header reader " << std::endl;
    std::clog << "-- macro_time_resolution: " << mt_clk
              << (rec.bits.femto ? " (femto units)" : " (0.1 ns units)") << std::endl;
    std::clog << "-- micro_time_resolution: " << mi_clk << " (default, see .set)" << std::endl;
    std::clog << "-- routing bits: " << rec.bits.n_routing_bits << std::endl;
    std::clog << "-- record layout: " << (rec.bits.six_channel ? "QC-x06" : "QC-x04") << std::endl;
}
    return 4;
}


// Minimal, dependency-free base64 codec used to carry the raw (largely binary)
// BH .set file through text-only header tags such as a PTU ANSI-string tag.
static std::string bh_base64_encode(const std::string& in){
    static const char* T =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    out.reserve(((in.size() + 2) / 3) * 4);
    size_t i = 0;
    while (i + 2 < in.size()){
        unsigned n = ((unsigned char)in[i] << 16) |
                     ((unsigned char)in[i+1] << 8) |
                     ((unsigned char)in[i+2]);
        out.push_back(T[(n >> 18) & 0x3F]);
        out.push_back(T[(n >> 12) & 0x3F]);
        out.push_back(T[(n >> 6) & 0x3F]);
        out.push_back(T[n & 0x3F]);
        i += 3;
    }
    if (i < in.size()){
        unsigned n = ((unsigned char)in[i] << 16);
        bool two = (i + 1 < in.size());
        if (two) n |= ((unsigned char)in[i+1] << 8);
        out.push_back(T[(n >> 18) & 0x3F]);
        out.push_back(T[(n >> 12) & 0x3F]);
        out.push_back(two ? T[(n >> 6) & 0x3F] : '=');
        out.push_back('=');
    }
    return out;
}

static std::string bh_base64_decode(const std::string& in){
    auto val = [](unsigned char c) -> int {
        if (c >= 'A' && c <= 'Z') return c - 'A';
        if (c >= 'a' && c <= 'z') return c - 'a' + 26;
        if (c >= '0' && c <= '9') return c - '0' + 52;
        if (c == '+') return 62;
        if (c == '/') return 63;
        return -1; // padding or whitespace
    };
    std::string out;
    out.reserve((in.size() / 4) * 3);
    int buf = 0, bits = 0;
    for (unsigned char c : in){
        int v = val(c);
        if (v < 0) continue; // skip '=' and any stray whitespace
        buf = (buf << 6) | v;
        bits += 6;
        if (bits >= 8){
            bits -= 8;
            out.push_back((char)((buf >> bits) & 0xFF));
        }
    }
    return out;
}

bool TTTRHeader::read_bh_set_file(const std::string& filename) {
    std::ifstream f(filename, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }

    // Preserve the full .set verbatim so a .spc+.set -> .ptu -> .spc+.set
    // conversion keeps every BH setting, not just the imaging keys tttrlib
    // interprets below. Real .set files are mostly binary (a binary preamble
    // plus text blocks), so the bytes are base64-encoded to ride safely through
    // text-only header tags (e.g. a PTU ANSI string) and are decoded back by
    // write_bh_set_file.
    std::stringstream buffer;
    buffer << f.rdbuf();
    std::string raw = buffer.str();
    if (!raw.empty()) {
        std::string b64 = bh_base64_encode(raw);
        add_tag(json_data(), "BH_SPC_SetFile",
                const_cast<char*>(b64.c_str()), tyAnsiString);
    }

    // TAC range and ADC resolution; only used for the SPC-QC modules, whose
    // 4 byte .spc header cannot carry the micro time resolution (see below)
    double tac_range = 0.0;
    int adc_resolution = 0;

    std::istringstream text(raw);
    std::string line;
    while (std::getline(text, line)) {
        // Remove leading/trailing whitespace
        size_t start = line.find_first_not_of(" \t\r\n");
        if (start == std::string::npos) continue;
        size_t end = line.find_last_not_of(" \t\r\n");
        line = line.substr(start, end - start + 1);

        if (line.empty() || line[0] == '*') {
            continue;
        }

        // Parse BH .set file format: "#SP [KEY,TYPE,VALUE]"
        // Example: "#SP [SP_IMG_X,I,512]"
        if (line.rfind("#SP [", 0) == 0) {
            size_t bracket_start = line.find('[');
            size_t bracket_end = line.find(']');
            if (bracket_start != std::string::npos && bracket_end != std::string::npos && bracket_end > bracket_start) {
                std::string content = line.substr(bracket_start + 1, bracket_end - bracket_start - 1);

                // Split by commas: "SP_IMG_X,I,512" -> key, type, value
                size_t first_comma = content.find(',');
                size_t last_comma = content.rfind(',');

                if (first_comma != std::string::npos && last_comma != std::string::npos && last_comma > first_comma) {
                    std::string key = content.substr(0, first_comma);
                    std::string val = content.substr(last_comma + 1);

                    try {
                        if (key == "SP_IMG_X") {
                            add_tag(json_data(), "ImgHdr_PixX", std::stoi(val), tyInt8);
                        } else if (key == "SP_IMG_Y") {
                            add_tag(json_data(), "ImgHdr_PixY", std::stoi(val), tyInt8);
                        } else if (key == "SP_PIX_CLK") {
                            int use_pixel_clock = (std::stoi(val) == 1) ? 1 : 0;
                            add_tag(json_data(), "BH_UsePixelClock", use_pixel_clock, tyInt8);
                        } else if (key == "SP_TAC_R") {
                            tac_range = std::stod(val);
                        } else if (key == "SP_ADC_RE") {
                            adc_resolution = std::stoi(val);
                        }
                    } catch (const std::exception& e) {
                        #ifdef VERBOSE_TTTRLIB
                        std::clog << "-- BH .set parse warning: skipping line with invalid value: " 
                                  << e.what() << std::endl;
                        #endif
                    } catch (...) {
                        #ifdef VERBOSE_TTTRLIB
                        std::clog << "-- BH .set parse warning: skipping line with unknown error" << std::endl;
                        #endif
                    }
                }
            }
        }
    }

    // The SPC-QC modules run the TAC independently of the macro time clock, so
    // the micro time resolution is not derivable from the .spc header. The .set
    // is the only place it is recorded; use it to replace the default assumed
    // by read_bh_spcqc_header.
    if(get_tttr_container_type() == BH_SPCQC_CONTAINER &&
       tac_range > 0.0 && adc_resolution > 0){
        add_tag(json_data(), TTTRTagRes, tac_range / (double) adc_resolution, tyFloat8);
        add_tag(json_data(), TTTRNMicroTimes, adc_resolution, tyInt8);
    }

    // Record that this is a BH SPC CLSM image so the reconstruction routine can
    // be picked automatically even after the data is transcoded to another
    // container (e.g. PTU). The frame/line markers are byte-preserved by the
    // record writers, so the BH_SPC130 routine reconstructs the image exactly
    // from any container. The hint rides along as a normal header tag.
    if(find_tag(json_data(), "ImgHdr_PixX") >= 0){
        add_tag(json_data(), "BH_SPC_ReadingRoutine",
                const_cast<char*>("BH_SPC130"), tyAnsiString);
    }
    return true;
}


bool TTTRHeader::write_bh_set_file(const std::string& filename, TTTRHeader* header){
    nlohmann::json &json = header->json_data();

    // Preferred path: an original .set was captured on read (directly or via a
    // PTU round trip). Re-emit it byte-for-byte so all BH settings are
    // preserved. The content is stored base64-encoded (see read_bh_set_file).
    if(find_tag(json, "BH_SPC_SetFile") >= 0){
        std::string b64 = get_tag(json, "BH_SPC_SetFile")["value"];
        std::string raw = bh_base64_decode(b64);
        if(!raw.empty()){
            std::ofstream f(filename, std::ios::binary);
            if(!f.is_open()) return false;
            f.write(raw.data(), (std::streamsize) raw.size());
            return true;
        }
    }

    // Fallback: synthesize a minimal .set from the imaging geometry when no
    // original was preserved (e.g. imaging tags set programmatically).
    bool has_x = find_tag(json, "ImgHdr_PixX") >= 0;
    bool has_y = find_tag(json, "ImgHdr_PixY") >= 0;
    if(!has_x && !has_y) return false;

    std::ofstream f(filename);
    if(!f.is_open()) return false;

    // Header block; lines starting with '*' are comments to the reader.
    f << "*SET_FILE created by tttrlib\n";
    f << "*BLOCK 1 SYS_PARA\n";
    if(has_x){
        int v = get_tag(json, "ImgHdr_PixX")["value"];
        f << "#SP [SP_IMG_X,I," << v << "]\n";
    }
    if(has_y){
        int v = get_tag(json, "ImgHdr_PixY")["value"];
        f << "#SP [SP_IMG_Y,I," << v << "]\n";
    }
    if(find_tag(json, "BH_UsePixelClock") >= 0){
        int v = get_tag(json, "BH_UsePixelClock")["value"];
        f << "#SP [SP_PIX_CLK,I," << (v ? 1 : 0) << "]\n";
    }
    f << "*END\n";
    return true;
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
    for (int32_t i = 0; i < num_channels; ++i) {
        uint32_t size;
        fread(&size, sizeof(size), 1, file);
        SwapEndian(size);
        fseek(file, size, SEEK_CUR);
    }

    add_tag(j, TTTRTagGlobRes, (double) header.col2_resolution, tyFloat8);

    // Return the current file position, which is the cursor
    return ftell64(file);
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
if (is_verbose()) {
    std::clog << "-- Confocor3 header reader " << std::endl;
    std::clog << "-- frequency_float: " << frequency_float << std::endl;
    std::clog << "-- measure_id_string: " << hex_measure_id << std::endl;
    std::clog << "-- macro_time_resolution: " << mt_clk << std::endl;
    std::clog << "-- channel_nbr: " << channel_nbr << std::endl;
    std::clog << "-- measurement_position: " << measurement_position << std::endl;
    std::clog << "-- kinetic_index: " << kinetic_index << std::endl;
    std::clog << "-- repetition_number: " << repetition_number << std::endl;
    std::clog << "-- header bytes: " << sizeof(rec) << std::endl;
}
    return static_cast<size_t>(ftell64(fpin));
}


size_t TTTRHeader::read_ht3_header(
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

#ifdef BUILD_PHOTON_HDF
// Helper function to process datasets in a given group
void TTTRHeader::process_hdf5_group_datasets(const HighFive::Group& group, const std::string group_name) {
    // Get all objects in the group
    auto object_names = group.listObjectNames();
    for (const auto& obj_name : object_names) {
if (is_verbose()) {
        std::cout << "Processing object: " << obj_name << std::endl;
}

        // Check if the object is a dataset
        if (group.getObjectType(obj_name) != HighFive::ObjectType::Dataset) {
if (is_verbose()) {
            std::cout << obj_name << " is not a dataset. Skipping." << std::endl;
}
            continue;
        }

        // Open the dataset
        auto dataset = group.getDataSet(obj_name);
        auto datatype = dataset.getDataType();
        auto dataspace = dataset.getSpace();
        auto dims = dataspace.getDimensions();

if (is_verbose()) {
        std::cout << "datatype in hdf: " << datatype.string() << std::endl;
        std::cout << "dims.size(): " << dims.size() << std::endl;
}

        // Process scalar or vector data
        if (dims.empty() || dims.size() == 1) {
            bool is_scalar = dims.empty() || dims[0] == 1;
            if (datatype == HighFive::AtomicType<int8_t>() || datatype == HighFive::AtomicType<uint8_t>() ||
                datatype == HighFive::AtomicType<int16_t>() || datatype == HighFive::AtomicType<uint16_t>() ||
                datatype == HighFive::AtomicType<int32_t>() || datatype == HighFive::AtomicType<uint32_t>() ||
                datatype == HighFive::AtomicType<int64_t>() || datatype == HighFive::AtomicType<uint64_t>()) {

                if (is_scalar) {
                    int value;
                    dataset.read(value);
if (is_verbose()) {
                    std::cout << obj_name << " (int): " << value << std::endl;
}
                    add_tag(json_data(), group_name + "." + obj_name, value, tyInt8, 0);
                } else {
                    std::vector<int> values;
                    dataset.read(values);
if (is_verbose()) {
                    std::cout << obj_name << " (int vector): ";
                    for (size_t idx = 0; idx < values.size(); ++idx) {
                        std::cout << values[idx] << " ";
                    }
                    std::cout << std::endl;
}
                    for (size_t idx = 0; idx < values.size(); ++idx) {
                        add_tag(json_data(), group_name + "." + obj_name, values[idx], tyInt8, static_cast<int>(idx));
                    }
                }
            } else if (datatype == HighFive::AtomicType<float>() || datatype == HighFive::AtomicType<double>()) {
                if (is_scalar) {
                    double value;
                    dataset.read(value);
                    add_tag(json_data(), group_name + "." + obj_name, value, tyFloat8, 0);
                } else {
                    std::vector<double> values;
                    dataset.read(values);
if (is_verbose()) {
                    std::cout << obj_name << " (float vector): ";
                    for (size_t idx = 0; idx < values.size(); ++idx) {
                        std::cout << values[idx] << " ";
                    }
                    std::cout << std::endl;
}
                    for (size_t idx = 0; idx < values.size(); ++idx) {
                        add_tag(json_data(), group_name + "." + obj_name, values[idx], tyFloat8, static_cast<int>(idx));
                    }
                }
            } else {
                std::string value;
                dataset.read(value);

                // Allocate memory and copy string data
                char* allocated_str = new char[value.size() + 2];
                std::strcpy(allocated_str, value.c_str());

                add_tag(json_data(), group_name + "." + obj_name, allocated_str, tyAnsiString, 0);

                // Free allocated memory
                delete[] allocated_str;
            }
        } else {
if (is_verbose()) {
            std::cerr << "Unsupported number of dimensions: " << dims.size() << " for " << obj_name << std::endl;
}
        }
    }
}

int TTTRHeader::read_photon_hdf5_setup(const char *fn) {
    try {
if (is_verbose()) {
        std::cout << "Opening file: " << fn << std::endl;
}
        // Open the HDF5 file using HighFive
        HighFive::File file(fn, HighFive::File::ReadOnly);

if (is_verbose()) {
        std::cout << "File opened successfully." << std::endl;
}
        json_data()["MeasDesc_ContainerType"] = PHOTON_HDF_CONTAINER;

        if (file.exist("/setup")) {
            process_hdf5_group_datasets(file.getGroup("/setup"), "setup");
        }
        if (file.exist("/identity")) {
            process_hdf5_group_datasets(file.getGroup("/identity"), "identity");
        }
        if (file.exist("/photon_data/timestamps_specs")) {
            process_hdf5_group_datasets(file.getGroup("/photon_data/timestamps_specs"), "timestamps_specs");
            double v = get_tag(json_data(), "timestamps_specs.timestamps_unit")["value"];
            add_tag(json_data(), TTTRTagGlobRes, v, tyFloat8);
        }
        if (file.exist("/photon_data/nanotimes_specs")) {
            process_hdf5_group_datasets(file.getGroup("/photon_data/nanotimes_specs"), "nanotimes_specs");
            int v1 = get_tag(json_data(), "nanotimes_specs.tcspc_num_bins")["value"];
            add_tag(json_data(), TTTRNMicroTimes, v1, tyInt8);
            double v2 = get_tag(json_data(), "nanotimes_specs.tcspc_unit")["value"];
            add_tag(json_data(), TTTRTagRes, v2, tyFloat8);
        }
        return 0; // Return success
    } catch (const HighFive::Exception& err) {
        std::cerr << "Error: " << err.what() << std::endl;
        return -1; // Return error
    }
}
#else

int TTTRHeader::read_photon_hdf5_setup(const char *fn) {
    (void) fn;
    return -1;
}

#endif

size_t TTTRHeader::read_ptu_header(
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
                std::cerr << "ERROR: PTU tyBinaryBlob not supported" << std::endl;
                fseek(fpin, (long) TagHead.TagValue, SEEK_CUR);
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

void TTTRHeader::ensure_minimal_tags(
        TTTRHeader* header, int container_type, size_t n_records){
    nlohmann::json &json = header->json_data();

    // Macro time resolution (seconds). Several writers (SPC-132, HT3, SM, CZ)
    // read this directly; a missing value makes them emit a garbage clock, so
    // always guarantee a positive value.
    if(find_tag(json, TTTRTagGlobRes) < 0){
        double v = header->get_macro_time_resolution();
        if(!(v > 0.0)) v = 1.0;
        add_tag(json, TTTRTagGlobRes, v, tyFloat8);
    }

    // Micro time (Dtime) resolution (seconds).
    if(find_tag(json, TTTRTagRes) < 0){
        double v = header->get_micro_time_resolution();
        if(!(v > 0.0)) v = 1.0;
        add_tag(json, TTTRTagRes, v, tyFloat8);
    }

    // Number of micro time channels.
    if(find_tag(json, TTTRNMicroTimes) < 0){
        int n = (int) header->get_number_of_micro_time_channels();
        if(n <= 0) n = 1;
        add_tag(json, TTTRNMicroTimes, n, tyInt8);
    }

    // PTU carries the record encoding and count explicitly; without these a
    // conforming PTU reader cannot parse the record stream.
    if(container_type == PQ_PTU_CONTAINER){
        if(find_tag(json, TTTRTagBits) < 0)
            add_tag(json, TTTRTagBits, 32, tyInt8);
        if(find_tag(json, TTTRTagNumRecords) < 0)
            add_tag(json, TTTRTagNumRecords, (int) n_records, tyInt8);
    }
}

void TTTRHeader::write_spc132_header(
        std::string fn, TTTRHeader* header, std::string mode){
    // write header
    bh_spc132_header_t head;
    head.allbits = 0;
    head.bits.unused = 0;
    head.bits.invalid = true;

    nlohmann::json tag = get_tag(header->json_data(), TTTRTagGlobRes);
    head.bits.macro_time_clock = (unsigned) ((double) tag["value"] * 10.e9);

    FILE* fp = fopen(fn.c_str(), mode.c_str());
    fwrite(&head, 4, 1, fp);
    fclose(fp);
}


void TTTRHeader::write_spcqc_header(
        std::string fn, TTTRHeader* header, std::string mode){
    bh_spcqc_header_t head;
    head.allbits = 0;
    head.bits.unused = 0;
    head.bits.invalid = true;
    head.bits.raw = 1;  // QC .spc files are always raw, never processed

    // The clock field is only 22 bit wide, so femtoseconds top out at 4.19 ns.
    // That covers every QC module, but not a macro clock inherited from another
    // container (a 50 ns PTU sync period needs 5e7 fs). Fall back to the classic
    // 0.1 ns unit in that case rather than truncating.
    const unsigned kClockMax = (1u << 22) - 1;
    double mt_clk = get_tag(header->json_data(), TTTRTagGlobRes)["value"];
    double femto_clock = mt_clk * 1e15;
    if (femto_clock <= (double) kClockMax) {
        head.bits.femto = 1;
        head.bits.macro_time_clock = (unsigned) (femto_clock + 0.5);
    } else {
        head.bits.femto = 0;
        double coarse = mt_clk * 1e10;
        head.bits.macro_time_clock =
                (unsigned) (coarse < (double) kClockMax ? coarse + 0.5 : kClockMax);
    }

    // Preserve the routing width and marker flag when they came from a QC file;
    // the record writer splits the channel on exactly this width, so the two
    // have to agree -- including the default for data that came from elsewhere
    // (see TTTR::spcqc_routing_shift).
    int idx = find_tag(header->json_data(), "BH_SPCQC_RoutingBits");
    head.bits.n_routing_bits = (idx >= 0)
            ? ((unsigned) (int) header->json_data()["tags"][idx]["value"] & 0xF)
            : (unsigned) BH_SPCQC_CH_SHIFT;
    idx = find_tag(header->json_data(), "BH_SPCQC_HasMarkers");
    if (idx >= 0) head.bits.markers =
            (unsigned) ((int) header->json_data()["tags"][idx]["value"] ? 1 : 0);

    // The channel field of the QC-x06 layout reaches into bit 30, so the reader
    // has to be told which layout the records use.
    head.bits.six_channel =
            (header->get_tttr_record_type() == BH_RECORD_TYPE_SPCQC_X06) ? 1 : 0;

    FILE* fp = fopen(fn.c_str(), mode.c_str());
    fwrite(&head, 4, 1, fp);
    fclose(fp);
}


void TTTRHeader::write_ptu_header(std::string fn, TTTRHeader* header, std::string modes){
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
        int idx = find_tag(header->json_data(), "Tag Version");
        if (idx >= 0)
            version_str = get_tag(header->json_data(), "Tag Version")["value"];
        else
            version_str = header->json_data()["Tag Version"];
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
    for(auto &it: header->json_data()["tags"].items()){
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




void TTTRHeader::write_ht3_header(std::string fn, TTTRHeader* header, std::string modes){
if (is_verbose()) {
    std::clog << "-- WRITE_HT3_HEADER" << std::endl;
}
    nlohmann::json &json = header->json_data();

    // Tag lookup helpers with defaults (get_tag returns a NONE tag when a
    // tag is missing, e.g. when transcoding from another container)
    auto tag_int = [&json](const std::string &name, int32_t d, int idx = -1) -> int32_t {
        if (TTTRHeader::find_tag(json, name, idx) < 0) return d;
        auto v = TTTRHeader::get_tag(json, name, idx)["value"];
        if (v.is_boolean()) return (int32_t) v.get<bool>();
        if (v.is_number()) return (int32_t) v.get<double>();
        return d;
    };
    auto tag_double = [&json](const std::string &name, double d) -> double {
        if (TTTRHeader::find_tag(json, name) < 0) return d;
        auto v = TTTRHeader::get_tag(json, name)["value"];
        return v.is_number() ? v.get<double>() : d;
    };
    auto tag_string = [&json](const std::string &name, const std::string &d) -> std::string {
        if (TTTRHeader::find_tag(json, name) < 0) return d;
        auto v = TTTRHeader::get_tag(json, name)["value"];
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
    int record_type = header->get_tttr_record_type();
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
    while (TTTRHeader::find_tag(json, "InputRate", n_channels) >= 0) n_channels++;
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
    if (TTTRHeader::find_tag(json, "ImgHdr") >= 0) {
        auto v = TTTRHeader::get_tag(json, "ImgHdr")["value"];
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


void TTTRHeader::write_sm_header(std::string fn, TTTRHeader* header, std::string modes){
if (is_verbose()) {
    std::clog << "-- WRITE_SM_HEADER" << std::endl;
}
    nlohmann::json &json = header->json_data();
    auto tag_int = [&json](const std::string &name, int32_t d) -> int32_t {
        if (TTTRHeader::find_tag(json, name) < 0) return d;
        auto v = TTTRHeader::get_tag(json, name)["value"];
        return v.is_number() ? (int32_t) v.get<double>() : d;
    };
    auto tag_double = [&json](const std::string &name, double d) -> double {
        if (TTTRHeader::find_tag(json, name) < 0) return d;
        auto v = TTTRHeader::get_tag(json, name)["value"];
        return v.is_number() ? v.get<double>() : d;
    };
    auto tag_string = [&json](const std::string &name, const std::string &d) -> std::string {
        if (TTTRHeader::find_tag(json, name) < 0) return d;
        auto v = TTTRHeader::get_tag(json, name)["value"];
        return v.is_string() ? v.get<std::string>() : d;
    };

    FILE* fp = fopen(fn.c_str(), modes.c_str());
    if (fp == nullptr) {
        std::cerr << "ERROR: Cannot write SM header to file: " << fn << std::endl;
        return;
    }

    // All values are stored big-endian (see read_sm_header)
    auto write_swapped = [&fp](auto value) {
        SwapEndian(value);
        fwrite(&value, sizeof(value), 1, fp);
    };
    // Strings are stored as a 32-bit big-endian length followed by the
    // characters including a terminating null byte
    auto write_string = [&](const std::string &s) {
        uint32_t size = (uint32_t) s.size() + 1;
        write_swapped(size);
        fwrite(s.c_str(), sizeof(char), size, fp);
    };

    write_swapped((uint32_t) tag_int("version", 1));
    write_string(tag_string("comment", "tttrlib"));
    write_string(tag_string("simple", ""));
    write_swapped((uint32_t) tag_int("pointer1", 0));
    write_string(tag_string("file_section_type", ""));
    write_swapped((uint32_t) tag_int("magic1", 0));
    write_swapped((uint32_t) tag_int("magic2", 0));

    double global_res = tag_double(TTTRTagGlobRes, 1.0);
    write_string(tag_string("col1_name", ""));
    write_swapped(tag_double("col1_resolution", 1.0));
    write_swapped(tag_double("col1_offset", 0.0));
    write_swapped((uint32_t) tag_int("col1_bho", 0));
    // The macro time resolution is stored as the column-2 resolution
    write_string(tag_string("col2_name", ""));
    write_swapped(tag_double("col2_resolution", global_res));
    write_swapped(tag_double("col2_offset", 0.0));
    write_swapped((uint32_t) tag_int("col2_bho", 0));
    write_string(tag_string("col3_name", ""));
    write_swapped(tag_double("col3_resolution", 1.0));
    write_swapped(tag_double("col3_offset", 0.0));

    // Channel labels are skipped on reading and hence not retained;
    // write zero channel labels to keep the header self-consistent.
    write_swapped((int32_t) 0);
    fclose(fp);
}


void TTTRHeader::write_cz_confocor3_header(std::string fn, TTTRHeader* header, std::string modes){
if (is_verbose()) {
    std::clog << "-- WRITE_CZ_CONFOCOR3_HEADER" << std::endl;
}
    nlohmann::json &json = header->json_data();
    auto tag_int = [&json](const std::string &name, int32_t d) -> int32_t {
        if (TTTRHeader::find_tag(json, name) < 0) return d;
        auto v = TTTRHeader::get_tag(json, name)["value"];
        return v.is_number() ? (int32_t) v.get<double>() : d;
    };
    auto tag_double = [&json](const std::string &name, double d) -> double {
        if (TTTRHeader::find_tag(json, name) < 0) return d;
        auto v = TTTRHeader::get_tag(json, name)["value"];
        return v.is_number() ? v.get<double>() : d;
    };
    auto tag_string = [&json](const std::string &name, const std::string &d) -> std::string {
        if (TTTRHeader::find_tag(json, name) < 0) return d;
        auto v = TTTRHeader::get_tag(json, name)["value"];
        return v.is_string() ? v.get<std::string>() : d;
    };

    cz_confocor3_settings_t settings;
    std::memset(&settings, 0, sizeof(settings));
    const char* ident = "Carl Zeiss ConfoCor3 - raw data";
    std::strncpy(settings.bits.Ident, ident, sizeof(settings.bits.Ident) - 1);
    // channel number is stored as an ASCII digit (see read_cz_confocor3_header)
    settings.bits.channel = '0' + (tag_int("channel", 1) & 0xFF);
    // measure_id is stored as a 32-character hex string tag
    std::string measure_id = tag_string("measure_id", "");
    for (int i = 0; i < 4; i++) {
        if (measure_id.size() >= (size_t)(i + 1) * 8) {
            settings.bits.measure_id[i] = (uint32_t) std::stoul(
                    measure_id.substr(i * 8, 8), nullptr, 16);
        }
    }
    // the reader reports these one-based
    settings.bits.measurement_position = (uint32_t) std::max(0, tag_int("measurement_position", 1) - 1);
    settings.bits.kinetic_index = (uint32_t) std::max(0, tag_int("kinetic_index", 1) - 1);
    settings.bits.repetition_number = (uint32_t) std::max(0, tag_int("repetition_number", 1) - 1);
    // the macro time clock is stored as a frequency
    double mt_clk = tag_double(TTTRTagGlobRes, 1.0);
    settings.bits.frequency = (uint32_t) std::llround(1.0 / mt_clk);

    FILE* fp = fopen(fn.c_str(), modes.c_str());
    if (fp == nullptr) {
        std::cerr << "ERROR: Cannot write CZ header to file: " << fn << std::endl;
        return;
    }
    fwrite(&settings, sizeof(settings), 1, fp);
    fclose(fp);
}








double TTTRHeader::get_macro_time_resolution(){
    double res;
    auto tag = get_tag(json_data(), TTTRTagGlobRes);
    if(tag["name"] == "NONE"){
        res = 1. / (double) get_tag(json_data(), TTTRSyncRate)["value"];
    } else{
        res = tag["value"];
    }
    return res;
}


std::string TTTRHeader::get_json(std::string tag_name, int idx, int indent){
    std::string s;
    if(tag_name.empty()){
        s = json_data().dump(indent);
    } else{
        int tag_idx = find_tag(json_data(), tag_name, idx);
        if(tag_idx >= 0){
            s = json_data()["tags"][tag_idx].dump(indent);
        } else {
            s = "{}";
        }
    }
    return s;
}


// ---------------------------------------------------------------------------
// Tag access.
//
// The implementations moved to the io layer (io/TTTRTags.h): they manipulate a
// nlohmann::json document and touch no TTTRHeader state at all, while every
// vendor header reader needs them. Leaving them here would have forced a format
// module to depend on core, and core to depend on the format modules -- a link
// cycle CMake refuses between shared libraries.
//
// These remain the public static API they have always been.
// ---------------------------------------------------------------------------

void TTTRHeader::add_tag(nlohmann::json &json_data, const std::string &name,
                         std::any value, unsigned int type, int idx) {
    tttrlib::io::add_tag(json_data, name, std::move(value), type, idx);
}

nlohmann::json TTTRHeader::get_tag(const nlohmann::json &json_data,
                                   const std::string &name, int idx) {
    return tttrlib::io::get_tag(json_data, name, idx);
}

int TTTRHeader::find_tag(nlohmann::json &json_data, const std::string &name, int idx) {
    return tttrlib::io::find_tag(json_data, name, idx);
}
