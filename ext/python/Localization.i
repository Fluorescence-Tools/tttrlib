//%{
//#include "Localization.h"
//%}
//
//
//%extend localization{
//
//
/////*wrapper for Fit2DGaussian function.
////params must contain 18(!) parameters, and can be called from python using a 1D
////numpy array.
////data contains the image to be fitted and can be called from python using
////a 2D numpy array.
////No new memory is created in this wrapper. Any memory allocated by subroutines
////is freed by subroutines.
////returns optimsed params vector*/
////ArrayXd Fit2DGaussian_pywrap(
////        ArrayXd vars,
////        MatrixXd data
////);
////
//
////params must contain 6 values to model a single Gaussian
////returns a filled model.
////*/
////MatrixXd model2DGaussian_pywrap(
////        ArrayXd vars,
////        MatrixXd model
////);
////
////    static double my_model2DGaussian(
////            double* param, int n_param,
////            double* mfunction, int n_mfunction,
////            short* fixed, int n_fixed, MParam* p){
////        if (n_x != 8) {
////            PyErr_Format(
////                    PyExc_ValueError,
////                    "The length of the parameter vector must of length 8 Arrays of length (%d) given", n_x);
////            return 0.0;
////        }
////        if (n_fixed < 4) {
////            PyErr_Format(
////                    PyExc_ValueError,
////                    "The length of the vector fixed be at least of length 6 Arrays of lengths (%d) given", n_fixed);
////            return 0.0;
////        }
////        return Localization::model2DGaussian(double *vars, double *model, int xlen, int ylen);
////    }
//
////params must contain 9 values to model two Gaussians
////returns a filled model.
////*/
////MatrixXd modelTwo2DGaussian_pywrap(
////        ArrayXd vars,
////        MatrixXd model
////);
//
////params must contain 12 values to model three Gaussians
////returns a filled model.
////*/
////MatrixXd modelThree2DGaussian_pywrap(
////        ArrayXd vars,
////        MatrixXd model
////);
////
//
////get two I star value for data modelled by model.
////Note: therms depending on data only have been dropped in the calculation
////therefore values may differ from other optimisers.
////*/
////double W2DG_pywrap(
////        MatrixXd data,
////        MatrixXd model
////);
//
//}
//
//%include "Localization.h"
