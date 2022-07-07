#ifndef TTTRLIB_CLSMPIXEL_H
#define TTTRLIB_CLSMPIXEL_H

#include "TTTR.h"
#include "TTTRRange.h"

class CLSMPixel : public TTTRRange{

    friend class CLSMLine;
    friend class CLSMImage;

public:

    virtual ~CLSMPixel() = default;

    CLSMPixel(const CLSMPixel& p2) : TTTRRange(p2){};

    CLSMPixel(CLSMPixel* p2 = nullptr){
        if(p2 != nullptr){
            _start = p2->_start;
            _stop = p2->_stop;
            _start_time = p2->_start_time;
            _stop_time = p2->_stop_time;
            _tttr_indices = p2->_tttr_indices;
        }
    }

    CLSMPixel& operator+=(const CLSMPixel& rhs){
        _tttr_indices.insert(
                std::end(_tttr_indices),
                std::begin(rhs._tttr_indices),
                std::end(rhs._tttr_indices)
        );

        return *this;
    }

};

#endif //TTTRLIB_CLSMPIXEL_H
