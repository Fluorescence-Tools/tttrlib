// SPDX-License-Identifier: BSD-3-Clause
#include "DecayFitData.h"


DecayFitData::DecayFitData(
        double dt,
        std::vector<double> corrections,
        std::vector<double> irf,
        std::vector<double> background,
        std::vector<int> data
) : corrections(std::move(corrections)), dt(dt) {
    // all channel numbers are multiplied by two for Jordi format
    size_t n_channels = std::max({irf.size() / 2, background.size() / 2, data.size() / 2});
    size_t n = 2 * n_channels;

    if (irf.size() != background.size()) {
        std::cerr << "WARNING: length of background pattern and IRF differ." << std::endl;
    }

    this->irf = std::move(irf);
    this->background = std::move(background);
    this->data = std::move(data);
    this->irf.resize(n, 0.0);
    this->background.resize(n, 0.0);
    this->data.resize(n, 0);
    this->model.assign(n, 0.0);
}


std::string DecayFitData::str() const {
    std::stringstream s;
    s << "DecayFitData:\n";
    s << "-- n_channels: " << n_channels() << "\n";
    s << "-- dt: " << dt << "\n";
    s << "-- corrections: ";
    for (auto v: corrections) s << v << ",";
    s << "\n";
    return s.str();
}
