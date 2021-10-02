#ifndef TTTRLIB_PDACALLBACK_H
#define TTTRLIB_PDACALLBACK_H

class PdaCallback{

public:

    virtual double run(double ch1, double ch2){
        return ch1 / ch2;
    }

    PdaCallback() = default;
    virtual ~PdaCallback()=default;
};

#include <vector>
#include <iostream>
#include <cmath>
#include "TTTR.h"

#endif //TTTRLIB_PDACALLBACK_H
