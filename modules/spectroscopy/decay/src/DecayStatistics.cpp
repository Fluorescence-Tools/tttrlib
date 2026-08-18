// SPDX-License-Identifier: BSD-3-Clause
#include <algorithm>
#include "DecayStatistics.h"
#include "Verbose.h"

#include <mutex>

const double twopi = 6.2831853071795865;
const double logtwopi = log(twopi);

// init factorial

static double logfact[150];
static std::once_flag logfact_once;


// overall log-likelihood w(C,M)
double twoIstar_1ch(const int* C, double* M, int Ndata)
{
    double W = 0., W0 = 0.;
    //int nempty = 0;
    for (int i=0; i<Ndata; i++)
        if (C[i]>0) {
            W += wcm(C[i], M[i]);
            W0 += wcm(C[i], (double)C[i]);
        }
        else {W += 1.; W0 += 1.;} // nempty++;
    return -2.*(W-W0)/(double)Ndata;
}



double statistics::chi2_counting(
        std::vector<double> &data,
        std::vector<double> &model,
        std::vector<double> &data_noise,
        int x_min,
        int x_max,
        const char* type
){
if (is_verbose()) {
    std::cout << "CHI2_COUNTING" << std::endl;
    std::cout << "-- type: " << type << std::endl;
    std::cout << "-- x_min: " << x_min << std::endl;
    std::cout << "-- x_max: " << x_max << std::endl;
}
    double chi2;
    if(strcmp(type, "neyman") == 0){
        chi2 = neyman(data.data(), model.data(), x_min, x_max);
    } else if(strcmp(type, "poisson") == 0){
        chi2 = poisson(data.data(), model.data(), x_min, x_max);
    } else if(strcmp(type, "pearson") == 0){
        chi2 = pearson(data.data(), model.data(), x_min, x_max);
    } else if(strcmp(type, "gauss") == 0){
        chi2 = gauss(data.data(), model.data(), x_min, x_max);
    } else if(strcmp(type, "cnp") == 0){
        chi2 = cnp(data.data(), model.data(), x_min, x_max);
    } else{
        chi2 = sswr(data.data(), model.data(), data_noise.data(), x_min, x_max);
    }
if (is_verbose()) {
    std::cout << "-- chi2: " << chi2 << std::endl;
}
    return chi2;
}

void init_fact()
{
  std::call_once(logfact_once, [] {
    double f = 1.;
    logfact[0] = 0.;
    for(int i = 1; i<150; i++) {
      f *= (double)i;
      logfact[i] = log(f);
    }
  });
}

double loggammaf(double t)
{
  return 0.5*(logtwopi-log(t))+t*(log(t+1./(12.*t-0.1/t))-1.);
}

double wcm(int C, double m)
{
    return C*log(m);
}

double wcm_p2s(int C, double mp, double ms)
{

  if (C==0) return 0.;

  // Out-of-range models: evaluate the series at the floor and subtract a
  // restoring term, instead of returning 0.
  //
  // Returning 0 had the same defect as Wcm's old skip (see DecayStatistics.h):
  // it is a discontinuous *improvement* in the minimised objective, so an
  // optimiser is rewarded for driving a bin to zero and finds nothing pulling
  // it back. This continuation is C0 -- the penalty vanishes at the floor -- and
  // strictly decreasing in how far below the floor the bin has gone, so there is
  // always a gradient home. It is not C1, because the series' true slope at the
  // floor is not the `C/m0` used here; matching it would mean differentiating
  // the sum, which is not worth it for a region no converged fit should visit.
  // Every call with both models at or above the floor is untouched.
  if ((mp<kModelFloor) || (ms<kModelFloor)) {
    const double mp_c = mp < kModelFloor ? kModelFloor : mp;
    const double ms_c = ms < kModelFloor ? kModelFloor : ms;
    double w = wcm_p2s(C, mp_c, ms_c);   // both args now >= floor: no recursion
    if (mp < kModelFloor) w += C * (mp - kModelFloor) / kModelFloor;
    if (ms < kModelFloor) w += C * (ms - kModelFloor) / kModelFloor;
    return w;
  }

  double s = 1., log1;

  double meanC = mp + 2.*ms, variance = mp + 4.*ms;
  double chi2w;

  // C > 500 => almost certainly overflow. Return chi2-type approximation
  if (C > 500)
  {
    chi2w = -0.5*(logtwopi + log(variance) + (C-meanC)*(C-meanC)/variance) + mp + ms;
    // where (+mp+ms) is needed to be consistent with w(C=0) = 0
    return chi2w;
  }

  // otherwise try to evaluate the sum
  // first term, Cs = 0
  if (C < 150)
    log1 = C*log(mp) - logfact[C];
  else		// cannot calculate factorial(C), use Stirling's approximation
    log1 = C*(log(mp) - log((double)C) + 1.) - 0.5*log(twopi*C);

  double w = s;
  double mfactor = ms / (mp * mp);

  int Cp, Csmax = C/2;
  for (int Cs=1; Cs<=Csmax; Cs++) {
    Cp = C - 2*Cs;
    s *= mfactor * (Cp + 2) * (Cp + 1) / (double)Cs;
    w += s;
  }

  if (std::isfinite(w)) return log(w) + log1;

  // if infinity, try another way around

  // first term, Cp = 0 or 1
  if (C < 150) log1 = Csmax*log(ms) - logfact[Csmax];
  else log1 = Csmax*(log(ms) - log((double)Csmax) + 1.) - 0.5*log(twopi*Csmax);
  if (C % 2) log1 += log(mp);

  s = 1.; w = 1.; mfactor = 1./mfactor;

  for (int Cs=Csmax-1; Cs>0; Cs--) {
    Cp = C - 2*Cs;
    s *= mfactor * (Cs + 1) / (double)((Cp - 1) * Cp);
    w += s;
  }

  if (std::isfinite(w)) return log(w) + log1;
  else return -0.5*(logtwopi + log(variance) + (C-meanC)*(C-meanC)/variance) + mp + ms; //chi2w
}

double Wcm_p2s(const int* C, double* M, int Nchannels)
{
  double W = 0.;
  for (int i=0; i<Nchannels; i++)
    W += wcm_p2s(C[i]+2*C[i+Nchannels], M[i], M[i+Nchannels]);

  return -W;
}

double twoIstar_p2s(const int* C, double* M, int Nchannels)
{
  double W = 0., W0 = 0., mp, ms;
  int Cp2s;
  for (int i=0; i<Nchannels; i++) {
    Cp2s = C[i]+2*C[i+Nchannels];
    mp = M[i];
    ms = M[i+Nchannels];
    W += wcm_p2s(Cp2s, mp, ms);
    W0 += wcm_p2s(Cp2s, C[i], C[i+Nchannels]);
    // this might be not 100% correct but anyhow not used in optimization
  }
  return -2.*(W-W0)/(double)Nchannels;
}

double twoIstar(const int* C, double* M, int Nchannels)
{
  double W = 0;
  for (int i=0; i<2*Nchannels; i++)
    if (C[i] > 0) W += C[i]*log(M[i]/(double)C[i]);

  return -W/(double)Nchannels;
}

double chi2_neyman(const int* C, double* M, int Nchannels)
{
  double chi2 = 0.;
  for (int i = 0; i < 2 * Nchannels; i++) {
    const double c = std::max(1.0, (double) C[i]);
    const double d = M[i] - (double) C[i];
    chi2 += d * d / c;
  }
  return chi2;
}

double chi2_gehrels(const int* C, double* M, int Nchannels)
{
  double chi2 = 0.;
  for (int i = 0; i < 2 * Nchannels; i++) {
    const double sigma = 1.0 + std::sqrt((double) C[i] + 0.75);
    const double d = M[i] - (double) C[i];
    chi2 += d * d / (sigma * sigma);
  }
  return chi2;
}

double decay_objective_score(int objective, const int* C, double* M, int Nchannels, bool report)
{
  switch (objective) {
    case kObjP2sMle:     return report ? twoIstar_p2s(C, M, Nchannels) : Wcm_p2s(C, M, Nchannels);
    case kObjNeymanLsq:  return chi2_neyman(C, M, Nchannels) / (report ? (double) (2 * Nchannels) : 1.0);
    case kObjGehrelsLsq: return chi2_gehrels(C, M, Nchannels) / (report ? (double) (2 * Nchannels) : 1.0);
    case kObjPoissonMle:
    default:             return report ? twoIstar(C, M, Nchannels) : Wcm(C, M, Nchannels);
  }
}

double Wcm(const int* C, double* M, int Nchannels)
{
  // log_m_ext is log() above kModelFloor and its C1 continuation below, in
  // place of the old silent skip -- see DecayStatistics.h for why a skip was a
  // reward rather than a guard.
  //
  // The multiply stays in the loop body rather than moving inside the helper,
  // so this is textually `W += C[i]*<a log>` exactly as before. That is not
  // fussiness: with the multiply inside the helper the compiler stops
  // contracting it into the accumulate, and every ordinary evaluation shifts by
  // an ulp. These functions are pinned by cross-language reference tests, and a
  // one-ulp drift in a fix that is supposed to change nothing above the floor is
  // a regression wearing a fix's clothes. test_decay_likelihood.cpp checks it
  // bitwise.
  double W = 0.;
  for (int i=0; i<2*Nchannels; i++)
    W += C[i]*log_m_ext(M[i]);
  return -W;
}
