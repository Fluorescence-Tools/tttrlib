// SPDX-License-Identifier: BSD-3-Clause
#include "io_cz.h"

#include <nlohmann/json.hpp>

#include <cstring>
#include <iostream>
#include <sstream>
#include <vector>

#include "FileIO.h"
#include "ByteOrder.h"
#include "TTTRTags.h"
#include "Verbose.h"

namespace tttrlib {
namespace io {

size_t read_cz_confocor3_header(
        std::FILE *fpin,
        nlohmann::json &data,
        bool rewind
) {
    if(rewind) std::fseek(fpin, 0, SEEK_SET);
    cz_confocor3_settings_t rec;
    fread(&rec, sizeof(rec),1, fpin);

    float frequency_float = rec.bits.frequency;
    if (frequency_float == 0.0f) frequency_float = 1.0f;
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
    delete[] hex_measure_id;
    return static_cast<size_t>(ftell64(fpin));
}

void write_cz_confocor3_header(std::string fn, nlohmann::json &data, std::string modes){
if (is_verbose()) {
    std::clog << "-- WRITE_CZ_CONFOCOR3_HEADER" << std::endl;
}
    nlohmann::json &json = data;
    auto tag_int = [&json](const std::string &name, int32_t d) -> int32_t {
        if (find_tag(json, name) < 0) return d;
        auto v = get_tag(json, name)["value"];
        return v.is_number() ? (int32_t) v.get<double>() : d;
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

    FILE* fp = open_file(fn, modes.c_str());
    if (fp == nullptr) {
        std::cerr << "ERROR: Cannot write CZ header to file: " << fn << std::endl;
        return;
    }
    fwrite(&settings, sizeof(settings), 1, fp);
    fclose(fp);
}

}  // namespace io
}  // namespace tttrlib
