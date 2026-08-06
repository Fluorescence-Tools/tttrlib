// SPDX-License-Identifier: BSD-3-Clause
#include "io_bh.h"

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
#include "TTTRRecordTypes.h"
#include "TTTRTags.h"
#include "Verbose.h"

namespace tttrlib {
namespace io {

// The BH ".set" sidecar stores the scan/marker configuration base64-encoded,
// so these travel with the sidecar reader that is their only caller.
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

size_t read_bh132_header(
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
    // Where the header ENDS, not how long it is. The two are the same number
    // for a file that is a container and different for one embedded in
    // something bigger, and every other header reader here returns the former.
    return static_cast<std::size_t>(std::ftell(fpin));
}

size_t read_bh_spcqc_header(
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
    // Where the header ENDS, not how long it is. The two are the same number
    // for a file that is a container and different for one embedded in
    // something bigger, and every other header reader here returns the former.
    return static_cast<std::size_t>(std::ftell(fpin));
}

bool read_bh_set_file(const std::string& filename, nlohmann::json &data) {
    std::ifstream f(filename, std::ios::binary);
    if (!f.is_open()) {
        return false;
    }
    std::stringstream whole;
    whole << f.rdbuf();
    return parse_bh_set(whole.str(), data);
}

bool parse_bh_set(const std::string& content, nlohmann::json &data) {
    // Preserve the full .set verbatim so a .spc+.set -> .ptu -> .spc+.set
    // conversion keeps every BH setting, not just the imaging keys tttrlib
    // interprets below. Real .set files are mostly binary (a binary preamble
    // plus text blocks), so the bytes are base64-encoded to ride safely through
    // text-only header tags (e.g. a PTU ANSI string) and are decoded back by
    // write_bh_set_file.
    const std::string& raw = content;
    if (!raw.empty()) {
        std::string b64 = bh_base64_encode(raw);
        add_tag(data, "BH_SPC_SetFile",
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
                            add_tag(data, "ImgHdr_PixX", std::stoi(val), tyInt8);
                        } else if (key == "SP_IMG_Y") {
                            add_tag(data, "ImgHdr_PixY", std::stoi(val), tyInt8);
                        } else if (key == "SP_PIX_CLK") {
                            int use_pixel_clock = (std::stoi(val) == 1) ? 1 : 0;
                            add_tag(data, "BH_UsePixelClock", use_pixel_clock, tyInt8);
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
    if((int) data[TTTRContainerType] == BH_SPCQC_CONTAINER &&
       tac_range > 0.0 && adc_resolution > 0){
        add_tag(data, TTTRTagRes, tac_range / (double) adc_resolution, tyFloat8);
        add_tag(data, TTTRNMicroTimes, adc_resolution, tyInt8);
    }

    // Record that this is a BH SPC CLSM image so the reconstruction routine can
    // be picked automatically even after the data is transcoded to another
    // container (e.g. PTU). The frame/line markers are byte-preserved by the
    // record writers, so the BH_SPC130 routine reconstructs the image exactly
    // from any container. The hint rides along as a normal header tag.
    if(find_tag(data, "ImgHdr_PixX") >= 0){
        add_tag(data, "BH_SPC_ReadingRoutine",
                const_cast<char*>("BH_SPC130"), tyAnsiString);
    }
    return true;
}

bool write_bh_set_file(const std::string& filename, nlohmann::json &data){
    nlohmann::json &json = data;

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

void write_spc132_header(
        std::string fn, nlohmann::json &data, std::string mode){
    // write header
    bh_spc132_header_t head;
    head.allbits = 0;
    head.bits.unused = 0;
    head.bits.invalid = true;

    nlohmann::json tag = get_tag(data, TTTRTagGlobRes);
    head.bits.macro_time_clock = (unsigned) ((double) tag["value"] * 10.e9);

    FILE* fp = fopen(fn.c_str(), mode.c_str());
    fwrite(&head, 4, 1, fp);
    fclose(fp);
}

void write_spcqc_header(
        std::string fn, nlohmann::json &data, std::string mode){
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
    double mt_clk = get_tag(data, TTTRTagGlobRes)["value"];
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
    int idx = find_tag(data, "BH_SPCQC_RoutingBits");
    head.bits.n_routing_bits = (idx >= 0)
            ? ((unsigned) (int) data["tags"][idx]["value"] & 0xF)
            : (unsigned) BH_SPCQC_CH_SHIFT;
    idx = find_tag(data, "BH_SPCQC_HasMarkers");
    if (idx >= 0) head.bits.markers =
            (unsigned) ((int) data["tags"][idx]["value"] ? 1 : 0);

    // The channel field of the QC-x06 layout reaches into bit 30, so the reader
    // has to be told which layout the records use.
    head.bits.six_channel =
            ((int) data[TTTRRecordType] == BH_RECORD_TYPE_SPCQC_X06) ? 1 : 0;

    FILE* fp = fopen(fn.c_str(), mode.c_str());
    fwrite(&head, 4, 1, fp);
    fclose(fp);
}

}  // namespace io
}  // namespace tttrlib
