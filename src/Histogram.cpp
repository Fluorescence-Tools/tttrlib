#include <include/Histogram.h>

void bincount1D(int* data, int n_data, int* bins, int n_bins){
    for(int j=0; j < n_data; j++)
    {
        int v = data[j];
        if( (v >= 0) && (v < n_bins) )
            bins[v]++;
    }
}

