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

    CLSMPixel(CLSMPixel* p2 = nullptr) : TTTRRange(){
        if(p2 != nullptr){
            _tttr_indices = p2->_tttr_indices;
        }
    }

};

#endif //TTTRLIB_CLSMPIXEL_H
