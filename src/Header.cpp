/****************************************************************************
 * Copyright (C) 2019 by Thomas-Otavio Peulen                               *
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

#ifdef _MSC_VER
#define _CRT_SECURE_NO_WARNINGS
#endif

#include <stdio.h>
#include <string>
#include <string.h>
#include <limits.h>

#include "../include/Header.h"
#include "../include/RecordReader.h"




Header::~Header(){};

Header::Header() :
        micro_time_resolution(0),
        macro_time_resolution(0),
        number_of_tac_channels(0),
        tttr_container_type(0)
{}


Header::Header(
        std::FILE *fpin,
        int tttr_container_type
        ) :
        Header()
{
    switch(tttr_container_type){
        case PQ_PTU_CONTAINER:
            number_of_tac_channels = 32768; //2**15
            header_end = read_ptu_header(
                    fpin,
                    true,
                    tttr_record_type,
                    data,
                    macro_time_resolution,
                    micro_time_resolution
                    );
            bytes_per_record = 4;
            break;
        case PQ_HT3_CONTAINER:
            header_end = read_ht3_header(
                    fpin,
                    true,
                    tttr_record_type,
                    data,
                    macro_time_resolution,
                    micro_time_resolution
                    );
            bytes_per_record = 4;
            number_of_tac_channels =     32768; //2**15
            break;
        case BH_SPC600_256_CONTAINER:
            header_end = 0;
            tttr_record_type = BH_RECORD_TYPE_SPC600_256;
            macro_time_resolution = 1.0;
            micro_time_resolution = 1.0;
            bytes_per_record = 4;
            number_of_tac_channels = 256;
            break;
        case BH_SPC600_4096_CONTAINER:
            header_end = 0;
            tttr_record_type = BH_RECORD_TYPE_SPC600_4096;
            macro_time_resolution = 1.0;
            micro_time_resolution = 1.0;
            bytes_per_record = 6;
            number_of_tac_channels = 4096;
            break;
        case BH_SPC130_CONTAINER:
            header_end = 0;
            tttr_record_type = BH_RECORD_TYPE_SPC130;
            macro_time_resolution = 1.0;
            micro_time_resolution = 1.0;
            bytes_per_record = 4;
            number_of_tac_channels = 4096;
            break;
        default:
            header_end = 0;
            tttr_record_type = BH_RECORD_TYPE_SPC130;
            macro_time_resolution = 1.0;
            micro_time_resolution = 1.0;
            bytes_per_record = 4;
            number_of_tac_channels = 4096;
            break;
    }
}


int Header::getTTTRRecordType(){
    return this->tttr_record_type;
}


size_t read_ht3_header(
        std::FILE *fpin,
        bool rewind,
        int &tttr_record_type,
        std::map<std::string, std::string> &data,
        double &macro_time_resolution,
        double &micro_time_resolution
) {

    if(rewind) std::fseek(fpin, 0, SEEK_SET);

    // Header of HT3 file
    pq_ht3_ascii_t ht3_header_ascii;
    fread(&ht3_header_ascii, 1, sizeof(ht3_header_ascii), fpin);

    pq_ht3_BinHdr_t ht3_header_binary;
    fread(&ht3_header_binary, 1, sizeof(ht3_header_binary), fpin);

    pq_ht3_BoardHdr ht3_header_board;
    fread(&ht3_header_board, 1, sizeof(ht3_header_board), fpin);

    //pq_ht3_TTTRHdr ht3_header_tttr_mode;
    //fread(&ht3_header_tttr_mode, 1, sizeof(ht3_header_tttr_mode), fpin);

    data["Ident"] = std::string(ht3_header_ascii.Ident);

    data["FormatVersion"] = std::string(ht3_header_ascii.FormatVersion);
    data["CreatorName"] = std::string(ht3_header_ascii.CreatorName);
    data["CreatorVersion"] = std::string(ht3_header_ascii.CreatorVersion);
    data["FileTime"] = std::string(ht3_header_ascii.FileTime);
    data["CRLF"] = std::string(ht3_header_ascii.CRLF);
    data["Comment"] = std::string(ht3_header_ascii.CommentField);

    data["Curves"] = std::to_string(ht3_header_binary.Curves);
    data["BitsPerChannel"] = std::to_string(ht3_header_binary.BitsPerChannel);
    data["RoutingChannels"] = std::to_string(ht3_header_binary.RoutingChannels);
    data["NumberOfBoards"] = std::to_string(ht3_header_binary.NumberOfBoards);
    data["ActiveCurve"] = std::to_string(ht3_header_binary.ActiveCurve);
    data["MeasMode"] = std::to_string(ht3_header_binary.MeasMode);
    data["SubMode"] = std::to_string(ht3_header_binary.SubMode);
    data["RangeNo"] = std::to_string(ht3_header_binary.RangeNo);
    data["Offset"] = std::to_string(ht3_header_binary.Offset);
    data["StopAt"] = std::to_string(ht3_header_binary.StopAt);
    data["StopOnOvfl"] = std::to_string(ht3_header_binary.StopOnOvfl);
    data["Restart"] = std::to_string(ht3_header_binary.Restart);
    data["DispLinLog"] = std::to_string(ht3_header_binary.DispLinLog);
    data["DispTimeFrom"] = std::to_string(ht3_header_binary.DispTimeFrom);
    data["DispTimeTo"] = std::to_string(ht3_header_binary.DispTimeTo);
    data["DispCountsFrom"] = std::to_string(ht3_header_binary.DispCountsFrom);
    data["DispCountsTo"] = std::to_string(ht3_header_binary.DispCountsTo);
    data["RepeatMode"] = std::to_string(ht3_header_binary.RepeatMode);
    data["RepeatsPerCurve"] = std::to_string(ht3_header_binary.RepeatsPerCurve);
    data["RepeatTime"] = std::to_string(ht3_header_binary.RepeatTime);
    data["RepeatWaitTime"] = std::to_string(ht3_header_binary.RepeatWaitTime);

    data["HardwareIdent"] = std::string(ht3_header_board.HardwareIdent);
    data["HardwareVersion"] = std::string(ht3_header_board.HardwareVersion);
    data["HardwareSerial"] = std::to_string(ht3_header_board.HardwareSerial);

    pq_ht3_board_settings_t board_settings;
    std::string key;
    for(int i=0; i<ht3_header_binary.NumberOfBoards; i++){
        if(fread(&board_settings, 1, sizeof(board_settings), fpin) == sizeof(board_settings)){
            key = "BoardSerial";
            key.append(std::to_string(i));
            data[key] = std::to_string(board_settings.BoardSerial);

            key = "SyncDivider";
            key.append(std::to_string(i));
            data[key] = std::to_string(board_settings.SyncDivider);

            key = "CFDZeroCross";
            key.append(std::to_string(i));
            data[key] = std::to_string(board_settings.SyncDivider);

            key = "CFDZeroLevel";
            key.append(std::to_string(i));
            data[key] = std::to_string(board_settings.BoardSerial);
        }
    }

    float resolution;
    if(fread(&resolution, 1, 4, fpin) == 4){
        data["Resolution"] = std::to_string(resolution);
    } else{
        data["Resolution"] = std::string("1");
    }

    macro_time_resolution = 1.0;
    micro_time_resolution = 1.0;

    // Todo: add identification of HydraHarp HHT3v1 files
    if (data["Ident"] == "HydraHarp")
    {
        if (data["FormatVersion"] == "1.0"){
            tttr_record_type = PQ_RECORD_TYPE_HHT3v1;
        } else{
            tttr_record_type = PQ_RECORD_TYPE_HHT3v2;
        }
    }
    else
    {
        tttr_record_type = PQ_RECORD_TYPE_PHT3;
    }
    return 880; // guessed by inspecting several ht3 files
}


size_t read_ptu_header(
        std::FILE *fpin,
        bool rewind,
        int &tttr_record_type,
        std::map<std::string, std::string> &data,
        double &macro_time_resolution,
        double &micro_time_resolution
) {
    /// The version of the PTU file
    char version[8];
    char Magic[8];

    if(rewind) std::fseek(fpin, 0, SEEK_SET);

    char Buffer[40];
    uint64_t tmp;
    char buffer_out[1024];
    char *AnsiBuffer;
    wchar_t *WideBuffer;
    std::string strFromChar;
    tag_head_t TagHead;
    uint64_t Result;
    uint64_t file_type = 0;
    // read the header

    tmp = fread(&Magic, 1, sizeof(Magic), fpin);
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
    data["Tag Version"] = buffer_out;

    do {
        // This loop is very generic. It reads all header items and displays the identifier and the
        // associated value, quite independent of what they mean in detail.
        // Only some selected items are explicitly retrieved and kept in memory because they are
        // needed to subsequently interpret the TTTR record data.
        Result = fread(&TagHead, 1, sizeof(TagHead), fpin);
        if (Result != sizeof(TagHead)) throw std::string("Incomplete File.");

        // Resolution for TCSPC-Decay
        if (strcmp(TagHead.Ident, TTTRTagRes) == 0)
            micro_time_resolution = *(double *) &(TagHead.TagValue) * 1e9;

        // Global macrotime_resolution for timetag in ns
        if (strcmp(TagHead.Ident, TTTRTagGlobRes) == 0)
            macro_time_resolution = *(double *) &(TagHead.TagValue)  * 1e9;
        // Number of records
        if (strcmp(TagHead.Ident, TTTRTagTTTRRecType) == 0)
            file_type = TagHead.TagValue;

        strcpy(Buffer, TagHead.Ident);
        if (TagHead.Idx > -1) sprintf(Buffer, "%s(%d)", TagHead.Ident, TagHead.Idx);

        switch (TagHead.Typ) {
            case tyEmpty8:
                sprintf(buffer_out, "<empty Tag>");
                break;
            case tyBool8:
                sprintf(buffer_out, "%s", bool(TagHead.TagValue) ? "True" : "False");
                break;
            case tyInt8:
                sprintf(buffer_out, "%lu", (unsigned long) TagHead.TagValue);
                break;
            case tyBitSet64:
                sprintf(buffer_out, "0x%16.16IX", TagHead.TagValue);
                break;
            case tyColor8:
                sprintf(buffer_out, "0x%16.16IX", TagHead.TagValue);
                break;
            case tyFloat8:
                sprintf(buffer_out, "%E", *(double *) &(TagHead.TagValue));
                break;
            case tyFloat8Array:
                sprintf(buffer_out, "<Float Array with %lu Entries>", TagHead.TagValue / sizeof(double));
                // only seek the Data, if one needs the data, it can be loaded here
                fseek(fpin, (long) TagHead.TagValue, SEEK_CUR);
                break;
            case tyTDateTime:
                double time;
                time = *(double *) &(TagHead.TagValue);
                time -= 25569;
                time *= 86400;
                sprintf(buffer_out, "%f", time);
                break;
            case tyAnsiString:
                AnsiBuffer = (char *) calloc((size_t) TagHead.TagValue, 1);
                Result = fread(AnsiBuffer, 1, (size_t) TagHead.TagValue, fpin);
                if (Result != TagHead.TagValue) {
                    free(AnsiBuffer);
                    throw std::string("Incomplete File.");
                }
                sprintf(buffer_out, "%s", AnsiBuffer);
                free(AnsiBuffer);
                break;
            case tyWideString:
                WideBuffer = (wchar_t *) calloc((size_t) TagHead.TagValue, 1);
                Result = fread(WideBuffer, 1, (size_t) TagHead.TagValue, fpin);
                if (Result != TagHead.TagValue) {
                    free(WideBuffer);
                    throw std::string("Incomplete File");
                }
                //fwprintf(buffer_out, L"%s", WideBuffer);
                free(WideBuffer);
                break;
            case tyBinaryBlob:
                sprintf(buffer_out, "<Binary Blob contains %lu Bytes>", TagHead.TagValue);
                // only seek the Data, if one needs the data, it can be loaded here
                fseek(fpin, (long) TagHead.TagValue, SEEK_CUR);
                break;
            default:
                throw std::string("Illegal Type identifier found! Broken file?");
        }
        data[TagHead.Ident] = buffer_out;
    }
    while ((strncmp(TagHead.Ident, FileTagEnd, sizeof(FileTagEnd))) != 0);

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
            throw std::string("\nFile type not supported.");
    }

    return (size_t) ftell(fpin);
}

