// SPDX-License-Identifier: BSD-3-Clause
#include "ImageLocalization.h"
#include "Registry.h"

#include "Dual.h"
#include "GradVec.h"

using namespace std;

namespace {

/// Model parameters of the three-Gaussian fit; entries 12..17 of `vars` are
/// flags and outputs, never optimised.
const int kNModelPar = 12;

/**
 * @brief Smooth reparameterisation of the bounded fit parameters.
 *
 * The optimiser previously enforced bounds by *teleporting* an out-of-range
 * parameter to the middle of its range (`varinbounds`), which is discontinuous:
 * a finite-difference step straddling a bound compared f(15.0) against f(7.5)
 * and divided by 3e-05, so that gradient component was meaningless. Positions
 * are now mapped through a logistic and strictly positive quantities through an
 * exponential, so every point in the search space is interior and the objective
 * is differentiable everywhere.
 *
 * The public `vars` vector keeps its original *constrained* meaning; the
 * transform is applied only around the optimiser.
 */
struct Reparam {
    double xlen, ylen;

    static double sigmoid(double u) { return 1.0 / (1.0 + std::exp(-u)); }
    static double logit(double p) { return std::log(p / (1.0 - p)); }

    /// Constrained -> unconstrained, for the optimiser's search space.
    void to_free(const double* c, double* u) const {
        const double span[6] = {xlen, ylen, 0, 0, 0, 0};
        // positions: x in (0, len)
        const int pos_idx[6] = {0, 1, 6, 7, 9, 10};
        const double pos_len[6] = {xlen, ylen, xlen, ylen, xlen, ylen};
        for (int k = 0; k < kNModelPar; ++k) u[k] = c[k];
        for (int k = 0; k < 6; ++k) {
            const int i = pos_idx[k];
            double p = c[i] / pos_len[k];
            p = std::min(std::max(p, 1e-6), 1.0 - 1e-6);  // keep logit finite
            u[i] = logit(p);
        }
        u[3] = std::log(std::max(c[3], 1e-9));   // sigma > 0
        u[4] = std::log(std::max(c[4], 1e-9));   // ellipticity > 0
        u[5] = std::log(std::max(c[5], 1e-9));   // background > 0
        (void)span;
    }

    /// Unconstrained -> constrained, templated so it also runs under AD.
    template <typename T>
    void to_bound(const T* u, T* c) const {
        const int pos_idx[6] = {0, 1, 6, 7, 9, 10};
        const double pos_len[6] = {xlen, ylen, xlen, ylen, xlen, ylen};
        for (int k = 0; k < kNModelPar; ++k) c[k] = u[k];
        for (int k = 0; k < 6; ++k) {
            const int i = pos_idx[k];
            c[i] = T(pos_len[k]) / (T(1.0) + exp(-u[i]));
        }
        c[3] = exp(u[3]);
        c[4] = exp(u[4]);
        c[5] = exp(u[5]);
    }
};

/// Everything the objective needs, passed through the optimiser's void*.
struct FitContext {
    GaussDataType* gdata;
    int n_gauss;      ///< 1, 2 or 3
    Reparam rp;
};

/// One 2D Gaussian evaluated into `model`; mirrors localization::model2DGaussian.
template <typename T>
void model_gauss(const T& x0, const T& y0, const T& A, const T& sigma, const T& eps,
                 const T& bg, T* model, int xlen, int ylen, bool accumulate) {
    const T tx = T(0.5) / (sigma * sigma);
    const T ty = T(0.5) / (sigma * sigma * eps * eps);
    std::vector<T> ex(xlen);
    for (int x = 0; x < xlen; ++x) {
        const T d = T(double(x)) - x0;
        ex[x] = exp(-d * d * tx);
    }
    int i = 0;
    for (int y = 0; y < ylen; ++y) {
        const T dy = T(double(y)) - y0;
        const T ey = exp(-dy * dy * ty);
        for (int x = 0; x < xlen; ++x) {
            const T v = A * ex[x] * ey + bg;
            if (accumulate) model[i] += v;
            else model[i] = v;
            ++i;
        }
    }
}

/**
 * @brief Poisson maximum-likelihood-ratio cost, as a pure function of the
 *        unconstrained parameters.
 *
 * Unlike the previous `target2DGaussian` this neither clamps nor writes back
 * into its input, so it can be differentiated.
 */
template <typename T>
T gauss_cost(const T* u, const FitContext& ctx) {
    const int xlen = ctx.gdata->xlen, ylen = ctx.gdata->ylen;
    const int osize = xlen * ylen;

    T c[kNModelPar];
    ctx.rp.to_bound(u, c);

    std::vector<T> model(osize);
    model_gauss<T>(c[0], c[1], c[2], c[3], c[4], c[5], model.data(), xlen, ylen, false);
    // secondary Gaussians share sigma/ellipticity and carry no background
    if (ctx.n_gauss >= 2)
        model_gauss<T>(c[6], c[7], c[8], c[3], c[4], T(0.0), model.data(), xlen, ylen, true);
    if (ctx.n_gauss >= 3)
        model_gauss<T>(c[9], c[10], c[11], c[3], c[4], T(0.0), model.data(), xlen, ylen, true);

    T w = T(0.0);
    const double* data = ctx.gdata->data;
    for (int i = 0; i < osize; ++i) {
        if (data[i] > 1e-12) w += model[i] - data[i] * log(model[i]);
        else w += model[i];
    }
    return w / double(osize);
}

/// Objective for the optimiser (unconstrained coordinates).
double gauss_target(double* u, void* p) {
    return gauss_cost<double>(u, *static_cast<FitContext*>(p));
}

/// Exact gradient in one forward-mode pass; returns f(u) as well.
///
/// A `Dual` whose derivative slot is an N-vector, seeded with the N basis
/// vectors, carries every partial through one evaluation of the objective --
/// where central differences would need 2N of them. Both pieces are the
/// library's own (modules/math); test/cpp/test_ad_gradient.cpp is what keeps
/// them honest.
double gauss_gradient(double* u, double* grad_out, void* p) {
    using Arr = tttrlib::GradVec<kNModelPar>;
    using DualN = tttrlib::Dual<Arr>;
    const FitContext& ctx = *static_cast<FitContext*>(p);

    DualN ud[kNModelPar];
    for (int j = 0; j < kNModelPar; ++j) ud[j] = DualN(u[j], Arr::Unit(j));
    const DualN r = gauss_cost<DualN>(ud, ctx);
    for (int j = 0; j < kNModelPar; ++j) grad_out[j] = r.grad[j];
    for (int j = kNModelPar; j < 18; ++j) grad_out[j] = 0.0;  // flags/outputs
    return r.val;
}

}  // namespace

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

int localization::fit2DGaussian(std::vector<double> &vars, std::vector<std::vector<double>> &data) {
    int xlen, ylen;
    ylen = static_cast<int>(data.size());
    if (ylen <= 0) {
        return -1;
    }
    xlen = static_cast<int>(data[0].size());
    if (xlen <= 0) {
        return -1;
    }
    for (const auto &row: data) {
        if (static_cast<int>(row.size()) != xlen) {
            return -1;
        }
    }

    //bfgs.minimize needs to take a void * as argument type
    //Therefore a pointer type is supplied
    GaussDataType *gdata_p;
    GaussDataType gdata;
    bfgs bfgs_o(gauss_target, 18); //optimisation object
    int osize = xlen * ylen;

    //reserve space for model
    std::vector<double> model;
    model.resize(osize);
    std::vector<double> flat_data;
    flat_data.reserve(osize);
    for (const auto &row: data) {
        flat_data.insert(flat_data.end(), row.begin(), row.end());
    }

    //fill gdata struct
    //gdata = { 0 };//compiler needs struct to be initialised
    gdata.data = flat_data.data();
    gdata.model = &model[0];
    gdata.xlen = xlen;
    gdata.ylen = ylen;
    gdata_p = &gdata;

    // Optimise in the smooth, unconstrained coordinates (see Reparam), with an
    // exact one-pass gradient instead of 2N central differences.
    FitContext ctx;
    ctx.gdata = gdata_p;
    ctx.n_gauss = (int) vars[16] + 1;
    ctx.rp.xlen = (double) xlen;
    ctx.rp.ylen = (double) ylen;
    bfgs_o.set_gradient(gauss_gradient);

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
    // the background is now mapped through exp(), which cannot reach zero
    if (vars[5] <= 0.0) vars[5] = 1e-3;
    if (vars[3] <= 0.0) vars[3] = 1.0;   // sigma
    if (vars[4] <= 0.0) vars[4] = 1.0;   // ellipticity

    // constrained -> unconstrained, minimise, then map back
    std::vector<double> work(vars.begin(), vars.end());
    ctx.rp.to_free(vars.data(), work.data());
    vars[12] = bfgs_o.minimize(work.data(), &ctx);
    {
        double bounded[kNModelPar];
        ctx.rp.to_bound(work.data(), bounded);
        for (int j = 0; j < kNModelPar; ++j) vars[j] = bounded[j];
    }

    // gdata.model still holds whatever the last objective call wrote, which is
    // no longer the optimum now that the objective is pure -- rebuild it at the
    // solution before scoring.
    {
        double c[kNModelPar];
        for (int j = 0; j < kNModelPar; ++j) c[j] = vars[j];
        model_gauss<double>(c[0], c[1], c[2], c[3], c[4], c[5],
                            gdata.model, xlen, ylen, false);
        if (ctx.n_gauss >= 2)
            model_gauss<double>(c[6], c[7], c[8], c[3], c[4], 0.0,
                                gdata.model, xlen, ylen, true);
        if (ctx.n_gauss >= 3)
            model_gauss<double>(c[9], c[10], c[11], c[3], c[4], 0.0,
                                gdata.model, xlen, ylen, true);
    }

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

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kGaussianLocalizationEntry = R"JSON({
  "name": "gaussian_localization",
  "label": "2-D Gaussian localisation (MLE / least squares) of emitters",
  "summary": "Fits a 2-D Gaussian (position, width, amplitude, background) to a spot or to every pixel neighbourhood of an image, with a photon threshold.",
  "description": "Per-spot maximum-likelihood / least-squares fit of a symmetric or elliptical 2-D Gaussian, the standard localisation model of single-molecule microscopy (Smith et al.), plus a whole-image driver that fits every candidate and masks the ones with too few photons. Returns positions, widths, amplitude, background and the goodness of fit; benchmarked against the reference implementations in `benchmarks/bench_localization.py`.",
  "operation_type": "molecule_localization",
  "method": "fit2DGaussian",
  "params_schema": {
    "type": "object",
    "properties": {
      "model": {
        "type": "string",
        "title": "Model",
        "default": "gaussian_2d"
      },
      "min_photons": {
        "type": "integer",
        "title": "Min photons",
        "default": 30
      },
      "max_iter": {
        "type": "integer",
        "title": "Max iterations",
        "default": 100
      }
    }
  },
  "inputs": {
    "required": [
      "image"
    ]
  },
  "outputs": {
    "columns": [
      "x",
      "y",
      "sigma_x",
      "sigma_y",
      "amplitude",
      "background",
      "chi2"
    ]
  },
  "row_grain": "molecule",
  "references": [
    {
      "type": "journal",
      "authors": "Smith, C. S., Joseph, N., Rieger, B., Lidke, K. A.",
      "title": "Fast, single-molecule localization that achieves theoretically minimum uncertainty",
      "journal": "Nat Methods",
      "year": 2010,
      "volume": "7",
      "pages": "373-375"
    }
  ],
  "api": [
    "localization",
    "ImageLocalizer",
    "fit_image",
    "GaussianFitResult",
    "GaussDataType"
  ],
  "can_replay": true
})JSON";
bool register_imagelocalization_entries() {
    tttrlib::register_algorithm_json("localization", "gaussian_localization", kGaussianLocalizationEntry);
    return true;
}
const bool kImageLocalizationRegistered = register_imagelocalization_entries();
}  // namespace
