// SPDX-License-Identifier: BSD-3-Clause
#include "io_sm.h"

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

size_t read_sm_header(FILE* file, nlohmann::json &j) {

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

void write_sm_header(std::string fn, nlohmann::json &data, std::string modes){
if (is_verbose()) {
    std::clog << "-- WRITE_SM_HEADER" << std::endl;
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

}  // namespace io
}  // namespace tttrlib
