// SPDX-License-Identifier: BSD-3-Clause
/*!
 * Plain container for the data of the MLE decay fits (fit23 - fit26):
 * experimental counts, instrument response, background pattern, corrections
 * and the model function, all in Jordi format (parallel channels followed by
 * perpendicular channels).
 *
 * Replaces the LabView-heritage MParam / LV*Array structures: the supported
 * interface is Python via SWIG, and plain std::vector members map directly
 * to Python sequences and numpy arrays.
 */
#ifndef TTTRLIB_DECAYFITDATA_H
#define TTTRLIB_DECAYFITDATA_H

#include <vector>
#include <string>
#include <sstream>
#include <algorithm>
#include <iostream>

class DecayFitData {

public:

    /// Experimental counting histogram (Jordi format)
    std::vector<int> data;

    /// Instrument response counting histogram (Jordi format)
    std::vector<double> irf;

    /// Background pattern (Jordi format); must be normalized outside
    std::vector<double> background;

    /// [excitation period, g factor, l1, l2, convolution stop]
    std::vector<double> corrections;

    /// Model function of the last fit / target evaluation (Jordi format)
    std::vector<double> model;

    /// Width of a micro time channel
    double dt = 1.0;

    /*!
     * Builds the container with all Jordi arrays sized consistently to
     * 2 * n_channels, where n_channels is derived from the longest of the
     * provided histograms. A single constructor (no overloads) keeps SWIG
     * keyword arguments available from Python.
     */
    DecayFitData(
            double dt = 1.0,
            std::vector<double> corrections = std::vector<double>(),
            std::vector<double> irf = std::vector<double>(),
            std::vector<double> background = std::vector<double>(),
            std::vector<int> data = std::vector<int>()
    );

    /// Number of micro time channels per polarization (Jordi arrays are 2x)
    int n_channels() const {
        return static_cast<int>(data.size() / 2);
    }

    /// Replace the experimental data (the model array is resized to match)
    void set_data(std::vector<int> d) {
        data = std::move(d);
        if (model.size() < data.size()) model.resize(data.size(), 0.0);
    }

    std::vector<int> get_data() const { return data; }
    std::vector<double> get_irf() const { return irf; }
    std::vector<double> get_background() const { return background; }
    std::vector<double> get_corrections() const { return corrections; }
    std::vector<double> get_model() const { return model; }

    std::string str() const;

};

#endif //TTTRLIB_DECAYFITDATA_H
