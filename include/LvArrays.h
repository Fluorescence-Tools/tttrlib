//!  Structures and functions used for LabView interface
/*!
 * fit2x was originally developed as a C backend for LabView software. Therefore,
 * the interface with fit2x uses structures that can be accessed by Labview. In
 * order to make an interfacing with Python and other languages possible there is
 * a this files defines a set of functions that facilitate the creation of the
 * LabView structures.
*/
#ifndef TTTRLIB_DECAYLVARRAYS_H
#define TTTRLIB_DECAYLVARRAYS_H

#include <iostream>
#include <cmath>
#include <algorithm>
#include <vector>
#include <string>
#include <sstream>


typedef struct LVI32Array{

    int length;
    int* data;

    ~LVI32Array(){
        free(data);
    }

    std::string str(){
        auto s = std::stringstream();
        s << "LVI32Array:\n";
        s << "-- length: " << length << std::endl;
        s << "-- data: " ;
        for(int i=0; i < length; i++){
            s << data[i] << ",";
        }
        return s.str();
    }

} LVI32Array;


typedef struct LVDoubleArray{

    int length;
    double* data;

    ~LVDoubleArray(){
        free(data);
    }

    std::string str(){
        auto s = std::stringstream();
        s << "LVDoubleArray:\n";
        s << "-- length: " << length << std::endl;
        s << "-- data: " ;
        for(int i=0; i < length; i++){
            s << data[i] << ",";
        }
        return s.str();
    }

} LVDoubleArray;


typedef struct MParam{

    LVI32Array** expdata;
    LVDoubleArray** irf;
    LVDoubleArray** bg;	// must be normalized outside!!!
    double dt;
    LVDoubleArray** corrections;
    LVDoubleArray** M;
    ~MParam(){
        delete *expdata;
        delete *irf;
        delete *bg;
        delete *corrections;
        delete *M;
    };

} MParam;


/*!
 *
 * @param len
 * @return
 */
LVI32Array *CreateLVI32Array(size_t len);

/*!
 *
 * @param len
 * @return
 */
LVDoubleArray *CreateLVDoubleArray(size_t len);


MParam* CreateMParam(
    double dt=1.0,
    std::vector<double> corrections = std::vector<double>(),
    std::vector<double> irf = std::vector<double>(),
    std::vector<double> background = std::vector<double>(),
    std::vector<int> data = std::vector<int>()
);

#endif //TTTRLIB_DECAYLVARRAYS_H
