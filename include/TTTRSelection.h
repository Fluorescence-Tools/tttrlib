#ifndef TTTRLIB_TTTRSELECTION_H
#define TTTRLIB_TTTRSELECTION_H

#include <memory>       /* shared_ptr */

#include "TTTRRange.h"

class TTTRSelection : public TTTRRange{

protected:

    std::shared_ptr<TTTR> _tttr = nullptr;

public:

    std::shared_ptr<TTTR> get_tttr(){
        //auto p = std::make_shared<TTTR>(*this, selection, n_selection, true);
        return _tttr;
    }

    void set_tttr(std::shared_ptr<TTTR> tttr){
        _tttr = tttr;
    }

    TTTRSelection(int start, int stop, std::shared_ptr<TTTR> tttr){
        _tttr = tttr;
        _tttr_indices.insert(start);
        _tttr_indices.insert(stop);
    }

    /// Copy constructor
    TTTRSelection(const TTTRSelection& p2){
        _tttr_indices = p2._tttr_indices;
        _tttr = p2._tttr;
    }

    TTTRSelection(std::shared_ptr<TTTR> tttr = nullptr){
        this->_tttr = tttr;
    }

};


#endif //TTTRLIB_TTTRSELECTION_H
