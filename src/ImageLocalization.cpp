#include "ImageLocalization.h"

using namespace std;

const int NVARS = 6;
const int NPEAKS_FACTOR = 10;

// background and threshold surfaces
static double *bg_surface = nullptr;
static int *threshold_surface = nullptr;
static int last_threshold = -108;

int osize;//object size
int osize_sq;//object area
int *icut;


double localization::target2DGaussian(double *vars, void *gdata_dummy) {
    double w;
    //convert void into GaussDataType.
    GaussDataType *gdata = (GaussDataType *) gdata_dummy;
    int osize = gdata->xlen * gdata->ylen;
    //	MGParam* p = (MGParam*)pM;
    //	LVDoubleArray *subimage = *(p->subimage), *M = *(p->M);

    vars[0] = varinbounds(vars[0], 0, (double) gdata->xlen);
    vars[1] = varinbounds(vars[1], 0, (double) gdata->ylen);
    vars[6] = varinbounds(vars[6], 0, (double) gdata->xlen);
    vars[7] = varinbounds(vars[7], 0, (double) gdata->ylen);
    vars[9] = varinbounds(vars[9], 0, (double) gdata->xlen);
    vars[10] = varinbounds(vars[10], 0, (double) gdata->ylen);
    vars[5] = varlowerbound(vars[5], 0); //if bg <0, bg = 1

    //get model
    if ((int) vars[16] == 0) {
        model2DGaussian(vars, gdata->model, gdata->xlen, gdata->ylen);
    } else if ((int) vars[16] == 1) {
        modelTwo2DGaussian(vars, gdata->model, gdata->xlen, gdata->ylen);
    } else if ((int) vars[16] == 2) {
        modelThree2DGaussian(vars, gdata->model, gdata->xlen, gdata->ylen);
    }
    w = W2DG(gdata->data, gdata->model, osize);
    return w;
}

double localization::varinbounds(double var, double min, double max) {
    if (var < min || var > max)
        var = (max - min) / 2;
    return var;
}

double localization::varlowerbound(double var, double min) {
    if (var < min)
        var = min + 1;
    return var;
}

int localization::model2DGaussian(double *vars, double *model, int xlen, int ylen) {
    //fill the matrix model with a Gaussian according to params
    //put parameters in more descriptive wordings
    int x, y;
    int i;
    double *ex = new double[xlen];
    double ey;
    double x0 = vars[0];
    double y0 = vars[1];
    double A = vars[2];
    double sigma = vars[3];
    double eps = vars[4];
    double bg = vars[5];
    double tx;
    double ty;
    tx = 0.5 / (sigma * sigma);
    ty = 0.5 / (sigma * sigma * eps * eps);

    //f(x,y) cn be written as ex(x)*ey(y)
    //first calc ex(x)
    for (x = 0; x < xlen; x++)
        ex[x] = exp(-(x - x0) * (x - x0) * tx);
    //now calc whole thing
    i = 0;
    for (y = 0; y < ylen; y++) {
        ey = exp(-(y - y0) * (y - y0) * ty);
        for (x = 0; x < xlen; x++) {
            model[i] = A * ex[x] * ey + bg;
            i++;
        }
    }
    delete[] ex;
    return 0;
}

int localization::modelTwo2DGaussian(double *vars, double *model, int xlen, int ylen) {
    int osize = xlen * ylen;
    double *vars_dummy = new double[6];
    double *model_dummy = new double[osize];
    int i;
    //create the first Gaussian with bg, store in model
    model2DGaussian(vars, model, xlen, ylen);

    //create the second Gaussian without bg, store in M_dummy
    vars_dummy[0] = vars[6];
    vars_dummy[1] = vars[7];
    vars_dummy[2] = vars[8];
    vars_dummy[3] = vars[3];
    vars_dummy[4] = vars[4];
    vars_dummy[5] = 0;
    model2DGaussian(vars_dummy, model_dummy, xlen, ylen);

    //add
    for (i = 0; i < osize; i++) {
        model[i] += model_dummy[i];
    }

    delete[] vars_dummy;
    delete[] model_dummy;
    return 0;
}

int localization::modelThree2DGaussian(double *vars, double *model, int xlen, int ylen) {
    int osize = xlen * ylen;
    double *vars_dummy = new double[6];
    double *model_dummy = new double[osize];
    int i;
    //create the first two Gaussian with bg, store in M
    modelTwo2DGaussian(vars, model, xlen, ylen);

    //create the third Gaussian without bg, store in M_dummy
    vars_dummy[0] = vars[9];
    vars_dummy[1] = vars[10];
    vars_dummy[2] = vars[11];
    vars_dummy[3] = vars[3];
    vars_dummy[4] = vars[4];
    vars_dummy[5] = 0;

    model2DGaussian(vars_dummy, model_dummy, xlen, ylen);

    //add
    for (i = 0; i < osize; i++) {
        model[i] += model_dummy[i];
    }

    delete[] vars_dummy;
    delete[] model_dummy;
    return 0;
}

int localization::fit2DGaussian(std::vector<double> vars, std::vector<std::vector<double>> &data) {
    int xlen, ylen;
    xlen = data.size();
    ylen = data[0].size();

    //bfgs.minimize needs to take a void * as argument type
    //Therefore a pointer type is supplied
    GaussDataType *gdata_p;
    GaussDataType gdata;
    bfgs bfgs_o(target2DGaussian, 18); //optimisation object
    int osize = xlen * ylen;

    //reserve space for model
    std::vector<double> model;
    model.resize(osize);

    //fill gdata struct
    //gdata = { 0 };//compiler needs struct to be initialised
    gdata.data = &data[0][0];
    gdata.model = &model[0];
    gdata.xlen = xlen;
    gdata.ylen = ylen;
    gdata_p = &gdata;

    //parameters 12-16 contain fit information and are fixed
    //1 Gauss fit uses first 6 parameters
    if ((int) vars[16] == 0) {
        for (int j = 6; j < 18; j++) bfgs_o.fix(j);
    }
    //2 Gauss fit uses first 9 parameters
    else if ((int) vars[16] == 1) {
        for (int j = 9; j < 18; j++) bfgs_o.fix(j);
    }
    //3 gauss fit uses first 12 parameters
    else if ((int) vars[16] == 2) {
        for (int j = 12; j < 18; j++) bfgs_o.fix(j);
    }

    //set levenberg-marquadt conversion parameters
    bfgs_o.seteps(1e-12); //this value has been converged on after testing
    bfgs_o.maxiter = 1000;

    //fix epsilon if indicated by function caller
    if (vars[15] == 1) {
        vars[4] = 1;
        bfgs_o.fix(4);
    }
    //fix bg if indicated by function caller
    if (vars[14] == 1) bfgs_o.fix(5);
    //if bg is free, make sure initial-guess is non-zero.
    //NV comment: do we really need this?
    if (vars[14] == 0 && vars[5] == 0) vars[5] = 0.1;

    vars[12] = bfgs_o.minimize(&vars[0], gdata_p);

    //get magnitude of tIstar for optimised solution
    vars[17] = twoIstar_G(gdata.data, gdata.model, osize);

    //delete gdata; //GaussDataType gdata is called with this function, therefore it is also deleted with this function right?
    return 1;
}

double localization::W2DG(double *data, double *model, int osize) {
    double w = 0.;
    for (int i = 0; i < osize; i++) {
        //avoid taking logarithm of 0
        if ((data[i] > 1.e-12) && (model[i] > 1.e-12)) {
            //all therms that are independant of the model are neglected as they do not contribute to the minimization
            w += model[i] - data[i] * log(model[i]);
        }
            // code saves taking expensive log when data[i] = 0
        else { w += model[i]; }    // Poisson-MLR (maximum likelihood ratio)
    }

    return w / (double) osize;
}


double localization::twoIstar_G(double *C, double *M, int osize) {
    double W = 0;
    for (int i = 0; i < osize; i++)
        if (C[i] > 0)
            W += M[i] - C[i] + C[i] * log(M[i] / C[i]);
    //W += C[i] * log(M[i] / C[i]);

    return -W / (double) osize;
}
