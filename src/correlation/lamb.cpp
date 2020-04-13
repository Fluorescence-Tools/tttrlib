#include "include/correlation/lamb.h"


void lamb::normalize(
        std::vector<unsigned long long> &x_axis,
        std::vector<double> &corr,
        std::vector<unsigned long long> &x_axis_normalized,
        std::vector<double> &corr_normalized,
        double cr1, double cr2,
        int n_bins, int n_casc, unsigned long long maximum_macro_time
){
    std::vector<double> divisor;

    // Compute the coarsening factor
    divisor.resize(x_axis.size());
    std::fill(divisor.begin(), divisor.end(), 1.0);
    int k = n_bins + 1;
    for(int j=0; j < n_casc; j++) {
        for (int i = 0; (i < n_bins) && (k<divisor.size()); i++) {
            divisor[k] = std::pow(2.0, (double) j);
            k++;
        }
    }
    for(int i=0; i < corr.size(); i++){
        auto delta_t = (double) (maximum_macro_time - x_axis[i]);
        corr_normalized[i] = corr[i] / (divisor[i] * delta_t * cr1 * cr2);
    }
}

void lamb::CCF(
        const unsigned long long *t1,
        const unsigned long long *t2,
        const double *weights1,
        const double *weights2,
        unsigned int nc,
        unsigned int nb,
        unsigned int np1,
        unsigned int np2,
        const unsigned long long *xdat, double *corrl
)
{
    // t1, t2:              macrotime vectors
    // xdat:                correlation time bins (timeaxis)
    // np1, np2:            number of photons in each channel
    // photons1, photons2:  photon weights
    // nc:                  number of evenly spaced elements per block
    // nb:                  number of blocks of increasing spacing
    // corrl:               pointer to correlation output
#if VERBOSE
    std::clog << "-- Copying data to new arrays..." << std::endl;
#endif
    // the arrays can be modified inplace during the correlation. Thus, copied to a new array.
    auto t1c = (unsigned long long *) malloc(sizeof(unsigned long long) * np1);
    std::memcpy(t1c, t1, sizeof(unsigned long long) * np1);
    auto t2c = (unsigned long long *) malloc(sizeof(unsigned long long) * np2);
    std::memcpy(t2c, t2, sizeof(unsigned long long) * np2);
    auto w1 = (double *) malloc(sizeof(double) * np1);
    std::memcpy(w1, weights1, sizeof(double) * np1);
    auto w2 = (double *) malloc(sizeof(double) * np2);
    std::memcpy(w2, weights2, sizeof(double) * np2);

    //Initializes some variables for for loops
    unsigned int i=0;
    unsigned int j=0;
    unsigned int k=0;
    unsigned int p=0;
    unsigned int im;

    //Initializes some parameters
    unsigned long long index;
    unsigned long long pw;
    unsigned long long limit_l;
    unsigned long long limit_r;

    // Goes through every block
    for (k=0;k<nb;k++)
    {
        // Determines spacing; used 2time spacing of one
        if (k==0) {pw=1;}
        else {pw=pow(2, k-1);};

        // p is the starting photon in second array
        p=0;

        // Goes through every photon in first array
        for (i=0;i<np1;i++) {
            //if (photons1[i]!=0)
            //{
            // Calculates minimal and maximal time for photons in second array
            limit_l= (unsigned long long)(xdat[k*nc]/pw + t1c[i]);
            limit_r= limit_l+nc;

            j=p;
            while ((j<np2) && (t2c[j] <= limit_r))
            {
                //if (photons2[j]!=0)
                //{
                if (k == 0) // Special Case for first round to include the zero time delay bin
                {
                    // If correlation time is positiv OR equal
                    if (t2c[j] >= limit_l)
                    {
                        // Calculates time between two photons
                        index= t2c[j] - limit_l + (unsigned long long)(k * nc);
                        // Adds one to correlation at the appropriate timelag
                        corrl[index]+=(double) (w1[i] * w2[j]);
                    }
                        // Increases starting photon in second array, to save time
                    else {p++;}
                }
                else
                {
                    // If correlation time is positiv
                    if (t2c[j] > limit_l)
                    {
                        // Calculates time between two photons
                        index= t2c[j] - limit_l + (unsigned long long)(k * nc);
                        // Adds one to correlation at the appropriate timelag
                        corrl[index]+=(double) (w1[i] * w2[j]);
                    }
                        // Increases starting photon in second array, to save time
                    else {p++;}
                }
                //}
                j++;
            };
            //};
        };
        //After second iteration;
        if (k>0)
        {
            // Bitwise shift right => Corresponds to dividing by 2 and rounding down
            // If two photons are in the same time bin, sums intensities and sets one to 0 to save calculation time
            for(im=0;im<np1;im++) { t1c[im] /= 2;};
            for(im=1;im<np1;im++)
            {
                if (t1c[im] == t1c[im - 1])
                { w1[im]+=w1[im - 1]; w1[im - 1]=0;};
            };
            for(im=0;im<np2;im++) { t2c[im] /= 2;};
            for(im=1;im<np2;im++)
            {
                if (t2c[im] == t2c[im - 1])
                { w2[im]+=w2[im - 1]; w2[im - 1]=0;};
            };
        };
        //
        j=0;
        for (i=0; i<np1; i++)
        {
            if (w1[i] != 0)
            {
                w1[j] = w1[i];
                t1c[j] = t1c[i];
                j++;
            }
        }
        np1=j;
        j=0;
        for (i=0; i<np2; i++)
        {
            if (w2[i] != 0)
            {
                w2[j] = w2[i];
                t2c[j] = t2c[i];
                j++;
            }
        }
        np2=j;
    }
}
