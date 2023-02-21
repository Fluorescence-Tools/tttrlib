//#ifndef TTTRLIB_LOCALIZATION_H
//#define TTTRLIB_LOCALIZATION_H
//
//#include <iostream>
//#include <cmath>
//#include <cinttypes>    /* uint64, int64, etc */
//
//#include "i_lbfgs.h"
//#include "LvArrays.h"
//
//
//typedef struct {
//    double *data; //contains the data to be fitted
//    double *model;//initialise empty array that will contain the model
//    int xlen;
//    int ylen; // for 1D data, ylen is unused
//} GaussDataType;
//
//
//// fitting options
//typedef struct {
//    char elliptical_circular;    // circular ?
//    double background;            // const BG input
//    char free_fixed;            // with free or fixed BG ?
//    int maxNPeaks;
//    char fit2DGauss;            // fit or just return cm's
//    char must_converge;            // discard peaks for which LM has not converged
//} OptsCluster;
//
//
//// fit results
//// legacy from Suren
//typedef struct {
//    unsigned int imageID;
//    unsigned int pixelID;
//    double peak_x;
//    double peak_y;
//    double intensity;
//    double chi2;
//    int lm_message;
//    double std;
//    double sigma_x;
//    double sigma_y;
//    double background;
//    double max_x;
//    double max_y;
//    double Ncounts;
//} ResultsCluster;
//
////This struct is legacy.
////Instead, use GaussDataType
//typedef struct {
//    LVDoubleArray **subimage;
//    int osize;
//    LVDoubleArray **M;
//} MGParam;
//
//
//class localization {
//
//protected:
//
//    /*!
//     *
//    check if var is within bounds
//    If out-of-bound, reset parameter to the middle of the bounds
//     * @param var
//     * @param min
//     * @param max
//     * @return
//     */
//    static inline double varinbounds(double var, double min, double max);
//
//    /*!
//    check if var is below lower bound
//    if yes, reset to lower-bound + 1
//     * @param var
//     * @param min
//     * @return
//     */
//    static inline double varlowerbound(double var, double min);
//
//    /*!
//     * overall -log-likelihood: Gauss2D
//     * @param data  array can represent 1D or 2D data
//     * Poisson-statistics governed data. data should contain ints, but is kept double for flexibility
//     * @param model model to fit the data
//     * @param osize length of array
//     * @return
//     */
//    static inline double W2DG(double *data, double *model, int osize);
//
//    /*!
//     * human-readable chisq_mle
//     * -2 * ln ( L ( C | M ) / L ( C | C ) )
//     *
//     * 2Istar value for human interpretation of result
//     * where L denotes the Likelihood, C the data and M the model.
//     * i.e. the found likelihood is devided by the likelihood if a 'perfect' solution is found.
//     * This function differs from twoIstar in the first to therms of the formula
//     * source: https://doi.org/10.1038/nmeth0510-338
//     * @param C Poisson-statistics governed data. data should contain ints, but is kept double for flexibility
//     * @param M model to fit the data
//     * @param osize length of array
//     * @return
//     */
//    static inline double twoIstar_G(double *C, double *M, int osize);
//
//    /// vars = [x0 y0 A sigma ellipticity bg]
//    static int model2DGaussian(double *vars, double *model, int xlen, int ylen);
//
//    /// function uses model2DGaussian function for constructor, see fit2DGaussian for parameter declaration
//    static int modelTwo2DGaussian(double *vars, double *model, int xlen, int ylen);
//
//    //function uses model2DGaussian and model2DGaussian function for constructor, see fit2DGaussian for parameter declaration
//    static int modelThree2DGaussian(double *vars, double *model, int xlen, int ylen);
//
//    /*
//    Function to minimize by bfgs object.
//    bfgs constructor needs function with arguments (double *, void*)
//    This function resets parameters within bounds, calculates model and gets goodness
//    Maybe these functionalities should be split up further?
//    */
//    static double target2DGaussian(double *vars, void *gdata);
//
//public:
//
//    /*!
//    * fit2DGaussian initializes optimisation routine
//    *
//    * @param vars vars contains the parameters that are optimized and has length 18!
//    * 	0: x0,
//    *	1: y0,
//    *	2: A0,
//    *	3: sigma,
//    *	4: ellipticity,
//    *	5: bg,
//    *	6: x1,
//    *	7: y1,
//    *	8: A1,
//    *	9: x2,
//    *	10: y2,
//    *	11: A2,
//    *	12: info, contains information from the fitting algorithm
//    *	13: wi_nowi, outdated
//    *	14: fit_bg, asks if background is fitted. 0 -> bg is fitted
//    *	15: ellipt_circ, determines if elliptical fits are allowed.  1-> eps is fixed.
//    *	16: model, determines the model to be used:
//    *	    0: model2DGaussian
//    *		1: modelTwo2DGaussian
//    *		2: modelThree2DGaussian
//    *	17: reserved for two Istar value of optimised solutio
//    * @param data
//    * @param xlen
//    * @param ylen
//    * @return
//    */
//    static int fit2DGaussian(std::vector<double> vars, std::vector<std::vector<double>> &data);
//
//    /*!
//     * double *image,                // one frame
//                                       int size_x, int size_y,        // image size
//                                       OptsCluster *options,
//                                       int Nimage,
//                                       int &Nall,               //  total number of peaks found
//                                       int &Ngood,            //  total number of good peaks found
//                                       int &Nconverged,    //  total number of converged peaks
//                                       int &Npeaks,            //  number of remaining peaks
//                                       void **presults,        // see ResultsCluster definition
//                                       int wi_nowi,            // weighted or no weights  ?
//                                       int Npeaks_tmp,            // number of found peaks
//                                       int *peak_x_tmp,        // x coordinates of loaded peaks
//                                       int *peak_y_tmp,        // y coordinates of loaded peaks
//                                       int input_estimated_bg,    // with input or estimated bg value?
//                                       MGParam *p)
//     * @return
//     */
//    static int Gauss2D_analysis_Ani(
//            std::vector<std::vector<double>> &image,   // one frame
//            OptsCluster *options,
//            int Nimage,
//            int &Nall,               //  total number of peaks found
//            int &Ngood,              //  total number of good peaks found
//            int &Nconverged,         //  total number of converged peaks
//            int &Npeaks,             //  number of remaining peaks
//            void **presults,         // see ResultsCluster definition
//            int wi_nowi,             // weighted or no weights  ?
//            int Npeaks_tmp,          // number of found peaks
//            int *peak_x_tmp,         // x coordinates of loaded peaks
//            int *peak_y_tmp,         // y coordinates of loaded peaks
//            int input_estimated_bg,  // with input or estimated bg value?
//            MGParam *p);
//
//
//};
//
//
//#endif //TTTRLIB_LOCALIZATION_H
