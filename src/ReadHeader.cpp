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


#include "../include/ReadHeader.h"

size_t ReadHeader::read_ht3_header() {

    fread(&ht3_header_ascii, 1, sizeof(ht3_header_ascii), fpin);
    fread(&ht3_header_binary, 1, sizeof(ht3_header_binary), fpin);
    fread(&ht3_header_board, 1, sizeof(ht3_header_board), fpin);
    fread(&ht3_header_tttr_mode, 1, sizeof(ht3_header_tttr_mode), fpin);

    header["Ident"] = std::string(ht3_header_ascii.Ident);
    header["FormatVersion"] = std::string(ht3_header_ascii.FormatVersion);
    header["CreatorName"] = std::string(ht3_header_ascii.CreatorName);
    header["CreatorVersion"] = std::string(ht3_header_ascii.CreatorVersion);
    header["FileTime"] = std::string(ht3_header_ascii.FileTime);
    header["CRLF"] = std::string(ht3_header_ascii.CRLF);
    header["Comment"] = std::string(ht3_header_ascii.CommentField);

    if (header["Ident"] == "HydraHarp")
    {
        processRecord = &ProcessHHT3v2;
    }
    else
    {
        processRecord = &ProcessPHT3;
    }
    return 880; // guessed by inspecting several ht3 files
}


size_t ReadHeader::read_ptu_header(std::FILE *fpin, bool rewind) {
    if(rewind) std::fseek(fpin, 0, SEEK_SET);

    char Buffer[40];
    uint64_t tmp;
    char buffer_out[1024];
    char *AnsiBuffer;
    wchar_t *WideBuffer;
    std::string strFromChar;
    tag_head_t TagHead;
    uint64_t Result;
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
    header["Tag Version"] = buffer_out;

    do {
        // This loop is very generic. It reads all header items and displays the identifier and the
        // associated value, quite independent of what they mean in detail.
        // Only some selected items are explicitly retrieved and kept in memory because they are
        // needed to subsequently interpret the TTTR record data.
        Result = fread(&TagHead, 1, sizeof(TagHead), fpin);
        if (Result != sizeof(TagHead)) throw std::string("Incomplete File.");

        // Resolution for TCSPC-Decay
        if (strcmp(TagHead.Ident, TTTRTagRes) == 0)
            macrotime_resolution = *(double *) &(TagHead.TagValue);
        // Global macrotime_resolution for timetag in ns
        if (strcmp(TagHead.Ident, TTTRTagGlobRes) == 0)
            microtime_resolution = *(double *) &(TagHead.TagValue);
        // Number of records
        if (strcmp(TagHead.Ident, TTTRTagNumRecords) == 0)
            n_records_in_file = TagHead.TagValue;
        // TTTR type_record
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
                sprintf(buffer_out, "%lld", TagHead.TagValue);
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
                sprintf(buffer_out, "<Float Array with %lld Entries>", TagHead.TagValue / sizeof(double));
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
                sprintf(buffer_out, "<Binary Blob contains %lld Bytes>", TagHead.TagValue);
                // only seek the Data, if one needs the data, it can be loaded here
                fseek(fpin, (long) TagHead.TagValue, SEEK_CUR);
                break;
            default:
                throw std::string("Illegal Type identifier found! Broken file?");
        }
        header[TagHead.Ident] = buffer_out;
    }
    while ((strncmp(TagHead.Ident, FileTagEnd, sizeof(FileTagEnd))) != 0);

    // select the reading routine
    switch (file_type) {
        case rtPicoHarpT2:
            processRecord = &ProcessPHT2;
            break;
        case rtPicoHarpT3:
            processRecord = &ProcessPHT3;
            break;
        case rtHydraHarpT2:
            processRecord = &ProcessHHT2v1;
            break;
        case rtMultiHarpNT2:
        case rtHydraHarp2T2:
        case rtTimeHarp260NT2:
        case rtTimeHarp260PT2:
            processRecord = &ProcessHHT2v2;
            break;
        case rtHydraHarpT3:
        case rtMultiHarpNT3:
        case rtHydraHarp2T3:
        case rtTimeHarp260NT3:
        case rtTimeHarp260PT3:
            processRecord = &ProcessHHT3v2;
            break;
        default:
            throw std::string("\nFile type not supported.");
    }

    return (size_t) ftell(fpin);
}

