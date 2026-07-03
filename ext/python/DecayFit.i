// SPDX-License-Identifier: BSD-3-Clause
%{
#include "DecayFit.h"
#include "DecayFitData.h"
#include <nlohmann/json.hpp>
using json = nlohmann::json;
%}

/* ------------------------------------------------------------------
 * 1) Renames — must appear before header inclusion
 * ------------------------------------------------------------------ */
%rename (modelf) my_modelf;
%rename (targetf) my_targetf;
%rename (fit) my_fit;

/* ------------------------------------------------------------------
 * 2) Typemap & helper includes — before all code using them
 * ------------------------------------------------------------------ */
%include <std_string.i>       // For std::string ↔ Python str

/* ------------------------------------------------------------------
 * 3) Apply typemaps to all pointer/length pairs
 *    (these apply to declarations parsed *after* this point)
 * ------------------------------------------------------------------ */
%apply (double* INPLACE_ARRAY1, int DIM1) {
    (double* x, int n_x),
    (double* param, int n_param),
    (double* irf, int n_irf),
    (double* bg, int n_bg),
    (double* corrections, int n_corrections),
    (double* mfunction, int n_mfunction),
    (double* model, int n_model)
}

%apply (short* IN_ARRAY1, int DIM1) {
    (short* fixed, int n_fixed)
}

%apply (int* IN_ARRAY1, int DIM1) {
    (int* data, int n_data)
}

/* If your headers use const/typedef variants, mirror them here:
%apply (const double* INPLACE_ARRAY1, int DIM1) {
    (const double* x, int n_x),
    (const double* param, int n_param)
}
*/

/* ------------------------------------------------------------------
 * 4) Core header and .i file inclusions
 * ------------------------------------------------------------------ */
%include "DecayFitData.h"
%include "DecayFit.h"

/* Python-side helper (pure Python) */
#ifdef SWIGPYTHON
%pythoncode "./ext/python/Fit2x.py"
#endif

/* Individual fit implementations */
#ifdef SWIGR
// SWIG-R names the wrapper's return value "result"; these native overloads take
// a parameter also named "result" which shadows it and breaks compilation
// (REAL(result) on a std::string). Suppress the native overloads for R; the
// %extend versions below (parameter renamed to "chi2") provide the same API.
%ignore DecayFit23::to_json(double const*, short const*, DecayFitData const*, double);
%ignore DecayFit23::fit_to_json(double const*, short const*, DecayFitData const*, double);
%ignore DecayFit23::modelf_to_json;
%ignore DecayFit24::to_json(double const*, short const*, DecayFitData const*, double);
%ignore DecayFit25::to_json(double const*, short const*, DecayFitData const*, double);
%ignore DecayFit26::to_json(double const*, short const*, DecayFitData const*, double);
#endif
%include "DecayFit23.i"
%include "DecayFit24.i"
%include "DecayFit25.i"
%include "DecayFit26.i"

/* ------------------------------------------------------------------
 * 5) Extend blocks — these now benefit from the typemaps above
 * ------------------------------------------------------------------ */
%extend DecayFitCorrections {
    std::string get_json() const {
        return $self->to_json().dump();
    }

    void set_json(const std::string& payload) {
        auto j = json::parse(payload);
        $self->from_json(j);
    }
};

%extend DecayFitSettings {
    std::string get_json() const {
        return $self->to_json().dump();
    }

    void set_json(const std::string& payload) {
        auto j = json::parse(payload);
        $self->from_json(j);
    }
};

%extend DecayFitIntegrateSignals {
    std::string get_json() const {
        return $self->to_json().dump();
    }

    void set_json(const std::string& payload) {
        auto j = json::parse(payload);
        $self->from_json(j);
    }
};

/* ------------------------------------------------------------------
 * 6) Extend DecayFit for parameter/data/model JSON handling
 * ------------------------------------------------------------------ */
%extend DecayFit {
    static std::string parameters_to_json(double* param, int n_param) {
        return DecayFit::parameters_to_json(param, n_param);
    }

    static void parameters_from_json(const std::string& payload,
                                     double* param, int n_param) {
        auto j = json::parse(payload);
        DecayFit::parameters_from_json(j, param, n_param);
    }

    static std::string data_to_json(int* data, int n_data) {
        return DecayFit::data_to_json(data, n_data);
    }

    static void data_from_json(const std::string& payload,
                               int* data, int n_data) {
        auto j = json::parse(payload);
        DecayFit::data_from_json(j, data, n_data);
    }

    static std::string model_to_json(double* model, int n_model) {
        return DecayFit::model_to_json(model, n_model);
    }

    static void model_from_json(const std::string& payload,
                                double* model, int n_model) {
        auto j = json::parse(payload);
        DecayFit::model_from_json(j, model, n_model);
    }
};

/* ------------------------------------------------------------------
 * 7) Extend the DecayFit23–26 families for JSON serialization
 * ------------------------------------------------------------------ */
%extend DecayFit23 {
    // NOTE: the last parameter is named "result" for Python/Java (unchanged API)
    // but "chi2" under -r: SWIG-R names the wrapper's return value "result", and a
    // C++ parameter of the same name shadows it (REAL(result) then fails to compile).
#ifndef SWIGR
    static std::string to_json(double* x, int n_x,
                               short* fixed, int n_fixed,
                               DecayFitData* p, double result) {
        return DecayFit23::to_json(x, fixed, p, result);
    }
#else
    static std::string to_json(double* x, int n_x,
                               short* fixed, int n_fixed,
                               DecayFitData* p, double chi2) {
        return DecayFit23::to_json(x, fixed, p, chi2);
    }
#endif

    static void from_json(const std::string& payload,
                          double* x, int n_x,
                          short* fixed, int n_fixed) {
        auto j = json::parse(payload);
        DecayFit23::from_json(j, x, fixed);
    }

#ifndef SWIGR
    static std::string fit_to_json(double* x, int n_x,
                                   short* fixed, int n_fixed,
                                   DecayFitData* p, double result) {
        return DecayFit23::fit_to_json(x, fixed, p, result);
    }
#else
    static std::string fit_to_json(double* x, int n_x,
                                   short* fixed, int n_fixed,
                                   DecayFitData* p, double chi2) {
        return DecayFit23::fit_to_json(x, fixed, p, chi2);
    }
#endif
};

%extend DecayFit24 {
#ifndef SWIGR
    static std::string to_json(double* x, int n_x,
                               short* fixed, int n_fixed,
                               DecayFitData* p, double result) {
        return DecayFit24::to_json(x, fixed, p, result);
    }
#else
    static std::string to_json(double* x, int n_x,
                               short* fixed, int n_fixed,
                               DecayFitData* p, double chi2) {
        return DecayFit24::to_json(x, fixed, p, chi2);
    }
#endif

    static void from_json(const std::string& payload,
                          double* x, int n_x,
                          short* fixed, int n_fixed) {
        auto j = json::parse(payload);
        DecayFit24::from_json(j, x, fixed);
    }
};

%extend DecayFit25 {
#ifndef SWIGR
    static std::string to_json(double* x, int n_x,
                               short* fixed, int n_fixed,
                               DecayFitData* p, double result) {
        return DecayFit25::to_json(x, fixed, p, result);
    }
#else
    static std::string to_json(double* x, int n_x,
                               short* fixed, int n_fixed,
                               DecayFitData* p, double chi2) {
        return DecayFit25::to_json(x, fixed, p, chi2);
    }
#endif

    static void from_json(const std::string& payload,
                          double* x, int n_x,
                          short* fixed, int n_fixed) {
        auto j = json::parse(payload);
        DecayFit25::from_json(j, x, fixed);
    }
};

%extend DecayFit26 {
#ifndef SWIGR
    static std::string to_json(double* x, int n_x,
                               short* fixed, int n_fixed,
                               DecayFitData* p, double result) {
        return DecayFit26::to_json(x, fixed, p, result);
    }
#else
    static std::string to_json(double* x, int n_x,
                               short* fixed, int n_fixed,
                               DecayFitData* p, double chi2) {
        return DecayFit26::to_json(x, fixed, p, chi2);
    }
#endif

    static void from_json(const std::string& payload,
                          double* x, int n_x,
                          short* fixed, int n_fixed) {
        auto j = json::parse(payload);
        DecayFit26::from_json(j, x, fixed);
    }
};
