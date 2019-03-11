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


#ifndef TTTRLIB_TTTRREADER_H
#define TTTRLIB_TTTRREADER_H

#include <string>
#include <iostream>
#include "definitions.h"
#include "TTTR.h"

class TTTRReader {

public:
    int file_containter_type;

    /// Determines based on the filename ending the file is a HT3 or PTU file
    int determine_file_container_type();

    /// Constructor
    TTTRReader::TTTRReader();

    /// @param fn the filename of the TTTR file
    TTTRReader::TTTRReader(const char *fn);

    /// Returns either a PQ or a BH class object (both are children from TTTR)
    auto getData();

private:
    /// the input file
    std::string filename;
};

#endif //TTTRLIB_TTTRREADER_H
