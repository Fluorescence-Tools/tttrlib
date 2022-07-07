#ifndef TTTRLIB_TTTRSELECTION_H
#define TTTRLIB_TTTRSELECTION_H

#include <memory>       /* shared_ptr */

#include "TTTRRange.h"

class TTTRSelection : public TTTRRange{

public:

    TTTR* get_tttr(){
        //auto p = std::make_shared<TTTR>(*this, selection, n_selection, true);
        return _tttr;
    }

    TTTRSelection(std::shared_ptr<TTTR> tttr = nullptr){
        this->_tttr = tttr.get();
    }


};


#endif //TTTRLIB_TTTRSELECTION_H
