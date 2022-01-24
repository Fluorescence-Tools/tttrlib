#ifndef TTTRLIB_TTTRSELECTION_H
#define TTTRLIB_TTTRSELECTION_H

#include <memory>       /* shared_ptr */

#include "TTTRRange.h"

class TTTRSelection : public TTTRRange{

public:

    std::shared_ptr<TTTR> get_tttr(){
        //auto p = std::make_shared<TTTR>(*this, selection, n_selection, true);
        return tttr;
    }

    TTTRSelection(std::shared_ptr<TTTR> tttr = nullptr){
        this->tttr = tttr;
    }


};


#endif //TTTRLIB_TTTRSELECTION_H
