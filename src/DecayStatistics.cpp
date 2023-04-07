#include "DecayStatistics.h"

const double twopi = 6.2831853071795865;
const double logtwopi = log(twopi);

// init factorial

static double logfact[150];


// overall log-likelihood w(C,M)
double twoIstar_1ch(int* C, double* M, int Ndata)
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
#ifdef VERBOSE_TTTRLIB
    std::cout << "CHI2_COUNTING" << std::endl;
    std::cout << "-- type: " << type << std::endl;
    std::cout << "-- x_min: " << x_min << std::endl;
    std::cout << "-- x_max: " << x_max << std::endl;
#endif
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
#ifdef VERBOSE_TTTRLIB
    std::cout << "-- chi2: " << chi2 << std::endl;
#endif
    return chi2;
}

void init_fact()
{
  double f = 1.;
  logfact[0] = 0.;
  for(int i = 1; i<150; i++) {
    f *= (double)i;
    logfact[i] = log(f);
  }
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
  if ((mp<1.e-12) || (ms<1.e-12)) return 0.;
  // ms and mp should not be <= 0, this is only for stability reasons

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

double Wcm_p2s(int* C, double* M, int Nchannels)
{
  double W = 0.;
  for (int i=0; i<Nchannels; i++)
    W += wcm_p2s(C[i]+2*C[i+Nchannels], M[i], M[i+Nchannels]);

  return -W;
}

double twoIstar_p2s(int* C, double* M, int Nchannels)
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

double twoIstar(int* C, double* M, int Nchannels)
{
  double W = 0;
  for (int i=0; i<2*Nchannels; i++)
    if (C[i] > 0) W += C[i]*log(M[i]/(double)C[i]);

  return -W/(double)Nchannels;
}

double Wcm(int* C, double* M, int Nchannels)
{
  double W = 0.;
  for (int i=0; i<2*Nchannels; i++)
    if (M[i]>1.e-12) W += C[i]*log(M[i]);
  return -W;
}