%{
#include "Localization.h"
%}

///*wrapper for Fit2DGaussian function.
//params must contain 18(!) parameters, and can be called from python using a 1D
//numpy array.
//data contains the image to be fitted and can be called from python using
//a 2D numpy array.
//No new memory is created in this wrapper. Any memory allocated by subroutines
//is freed by subroutines.
//returns optimsed params vector*/
//ArrayXd Fit2DGaussian_pywrap(
//        ArrayXd vars,
//        MatrixXd data
//);
//

//params must contain 6 values to model a single Gaussian
//returns a filled model.
//*/
//MatrixXd model2DGaussian_pywrap(
//        ArrayXd vars,
//        MatrixXd model
//);
//

//params must contain 9 values to model two Gaussians
//returns a filled model.
//*/
//MatrixXd modelTwo2DGaussian_pywrap(
//        ArrayXd vars,
//        MatrixXd model
//);

//params must contain 12 values to model three Gaussians
//returns a filled model.
//*/
//MatrixXd modelThree2DGaussian_pywrap(
//        ArrayXd vars,
//        MatrixXd model
//);
//

//get two I star value for data modelled by model.
//Note: therms depending on data only have been dropped in the calculation
//therefore values may differ from other optimisers.
//*/
//double W2DG_pywrap(
//        MatrixXd data,
//        MatrixXd model
//);

%include "Localization.h"