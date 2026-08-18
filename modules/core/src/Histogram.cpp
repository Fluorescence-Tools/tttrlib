// SPDX-License-Identifier: BSD-3-Clause
#include "Histogram.h"
#include "Registry.h"

void bincount1D(int* data, int n_data, int* bins, int n_bins){
    for(int j=0; j < n_data; j++)
    {
        int v = data[j];
        if( (v >= 0) && (v < n_bins) )
            bins[v]++;
    }
}

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kHistogramEntry = R"JSON({
  "name": "histogram",
  "label": "Histograms (1-D, 2-D, N-D, weighted, log/linear axes)",
  "summary": "Bins data into 1-D, 2-D and N-D histograms with linear or logarithmic axes, weights, and threaded fill; the `Histogram` / `HistogramNd` classes and their function forms.",
  "description": "The library's binning: fixed-width and edge-defined axes, linear and log10 spacing, weighted counts, out-of-range handling, and a partitioned multi-threaded fill for large inputs; N-D via `HistogramNd`. Feature parity with boost-histogram at higher speed (benchmarked). Every micro-time histogram, E histogram and image intensity in the library is built with these.",
  "operation_type": "histogram_construction",
  "method": "histogram",
  "params_schema": {
    "type": "object",
    "properties": {
      "bins": {
        "type": "integer",
        "title": "Bins",
        "default": 256
      },
      "axis_type": {
        "type": "string",
        "title": "Axis",
        "default": "lin",
        "enum": [
          "lin",
          "log10"
        ]
      },
      "weights": {
        "type": "boolean",
        "title": "Weighted",
        "default": false
      }
    }
  },
  "inputs": {
    "required": [
      "values"
    ],
    "optional": [
      "weights"
    ]
  },
  "outputs": {
    "columns": [
      "counts",
      "bin_edges"
    ]
  },
  "row_grain": "curve_point",
  "references": [
    {
      "type": "book",
      "authors": "Becker, W.",
      "title": "Advanced Time-Correlated Single Photon Counting Techniques",
      "publisher": "Springer",
      "year": 2005
    }
  ],
  "api": [
    "HistogramNd",
    "doubleHistogram",
    "doubleAxis",
    "Axis",
    "AxisOptions",
    "AxisVector",
    "histogram",
    "histogram1D_double",
    "histogram1D_int",
    "histogram1D_range_double",
    "histogram2D_double",
    "histogram2D_int",
    "histogram2D_range_double",
    "histogram2d",
    "histogramdd",
    "bincount1D",
    "fill_histogram",
    "fill_histogram_sample",
    "make_bin_edges_double",
    "histogram_fill_threads",
    "histogram_should_partition",
    "category_axis_for",
    "TTTR.get_microtime_histogram",
    "StreamingDecayHistogram",
    "StreamingIntensityTrace"
  ],
  "can_replay": true
})JSON";
bool register_histogram_entries() {
    tttrlib::register_algorithm_json("histogram", "histogram", kHistogramEntry);
    return true;
}
const bool kHistogramRegistered = register_histogram_entries();
}  // namespace
