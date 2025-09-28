#ifndef TTTRLIB_CLSMPIXEL_H
#define TTTRLIB_CLSMPIXEL_H

#include <utility>

#include "TTTR.h"
#include "TTTRSelection.h"

class CLSMPixel : public TTTRSelection{

    friend class CLSMLine;
    friend class CLSMImage;

public:

    virtual ~CLSMPixel() = default;

    CLSMPixel(){
        set_dense(true);
    }

    CLSMPixel(const CLSMPixel& p2) : TTTRSelection(p2){}

    explicit CLSMPixel(std::shared_ptr<TTTR> tttr) : TTTRSelection(std::move(tttr)){
        set_dense(true);
    }

    CLSMPixel& operator=(const CLSMPixel& other) = default;

};

#endif //TTTRLIB_CLSMPIXEL_H
