#ifndef TTTRLIB_STATISTICS_H
#define TTTRLIB_STATISTICS_H

#include <cmath>
#include <vector>
#include <numeric>
#include <string>
#include <algorithm>


namespace statistics{

    /*!
     * Different chi2 measures for counting data:
     *
     * https://arxiv.org/pdf/1903.07185.pdf
     *
     * @param data
     * @param model
     * @param x_min
     * @param x_max
     * @param type
     * @return
     */
    double chi2_counting(
            std::vector<double> &data,
            std::vector<double> &model,
            int x_min = -1,
            int x_max = -1,
            std::string type="neyman"
    );
}

#endif //TTTRLIB_STATISTICS_H
