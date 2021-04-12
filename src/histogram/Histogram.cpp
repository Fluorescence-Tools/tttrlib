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

#include <include/Histogram.h>

void bincount1D(int* data, int n_data, int* bins, int n_bins){
    for(int j=0; j < n_data; j++)
    {
        int v = data[j];
        if( (v >= 0) && (v < n_bins) )
            bins[v]++;
    }
}

