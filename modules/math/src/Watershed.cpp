// SPDX-License-Identifier: BSD-3-Clause
// Bit-exact port of scikit-image (>= 0.25.1), read the header for the history:
// the flood seeds the queue with markers at their image value (0.25.0 briefly
// used `-inf`, reverted upstream in PR 7702), and the marching-squares case
// bits are `ul=1, ur=2, ll=4, lr=8`. `#pragma STDC FP_CONTRACT OFF` is load-bearing
// for the same reason KMeans.cpp and Kalman.cpp carry it: `_fraction` below is
// `(level - from) / (to - from)`, and a fused evaluation of that expression
// rounds once instead of twice and moves a contour endpoint by a ulp, which
// breaks the exactness pin.
#pragma STDC FP_CONTRACT OFF
#include "Watershed.h"
#include "Registry.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace tttrlib {
namespace {

void require(bool ok, const char* kernel, const char* what) {
    if (!ok) throw std::invalid_argument(std::string(kernel) + ": " + what);
}

// Linear interpolation along an edge, exactly as skimage's `_get_fraction`.
// Equal corners are load-bearing: the reference returns 0 for a degenerate
// edge, which is what keeps a block whose corners straddle the level from
// producing a spurious endpoint.
inline double fraction(double from_value, double to_value, double level) {
    if (to_value == from_value) return 0.0;
    return (level - from_value) / (to_value - from_value);
}

// Raveled neighbour offsets of a (3, 3) footprint centred on (1, 1), ordered
// by Euclidean distance with a stable sort -- the order is part of the
// algorithm, not a detail: age, the tie-break on a plateau, is assigned in
// this order, so a different order divides a plateau differently. `W` is the
// padded image width; offsets are `dr * W + dc`, so border pixels must be
// masked off (see watershed()).
std::vector<long long> neighbour_offsets(int W, int connectivity) {
    std::vector<std::pair<int, int>> cells;
    for (int dr = -1; dr <= 1; ++dr)
        for (int dc = -1; dc <= 1; ++dc)
            if (std::abs(dr) + std::abs(dc) <= connectivity)
                cells.emplace_back(dr, dc);
    std::stable_sort(cells.begin(), cells.end(),
                     [](const std::pair<int, int>& a, const std::pair<int, int>& b) {
                         return (long long)a.first * a.first + (long long)a.second * a.second <
                                (long long)b.first * b.first + (long long)b.second * b.second;
                     });
    std::vector<long long> out;
    out.reserve(cells.size() - 1);
    for (size_t i = 1; i < cells.size(); ++i)  // skip the centre, distance 0
        out.push_back((long long)cells[i].first * W + cells[i].second);
    return out;
}

// The flood itself, a direct port of ChiSurf's `_flood` (which agrees with
// skimage >= 0.25.1; only 0.25.0 seeded markers at `-inf`). Min-heap on
// `(value, age)`; `age` is the entry order and
// is what splits a plateau evenly between the markers on either side of it.
// `output` carries the padded labels and must be nonzero exactly at the
// markers on entry; labels are assigned at push time, which is correct for the
// plain (non-compact, no-line) watershed because a pixel can never be reached
// more cheaply later.
void flood(const double* image, const std::vector<long long>& marker_locations,
           const std::vector<long long>& offsets, const unsigned char* mask,
           long long* output) {
    const double neg_inf = -std::numeric_limits<double>::infinity();
    std::vector<double> heap_value;
    std::vector<long long> heap_age, heap_index, heap_source;
    heap_value.reserve(8 * marker_locations.size() + 64);
    heap_age.reserve(heap_value.capacity());
    heap_index.reserve(heap_value.capacity());
    heap_source.reserve(heap_value.capacity());
    size_t size = 0;

    auto push = [&](double value, long long age, long long index, long long source) {
        size_t position = size;
        heap_value.push_back(value);
        heap_age.push_back(age);
        heap_index.push_back(index);
        heap_source.push_back(source);
        size += 1;
        while (position > 0) {
            const size_t parent = (position - 1) / 2;
            if (heap_value[parent] < value ||
                (heap_value[parent] == value && heap_age[parent] <= age))
                break;
            heap_value[position] = heap_value[parent];
            heap_age[position] = heap_age[parent];
            heap_index[position] = heap_index[parent];
            heap_source[position] = heap_source[parent];
            position = parent;
        }
        heap_value[position] = value;
        heap_age[position] = age;
        heap_index[position] = index;
        heap_source[position] = source;
    };

    // Markers enter at their own image value (scikit-image >= 0.25.1, PR 7702,
    // which reverted 0.25.0's `-inf` seeding: a marker placed at a maximum no
    // longer floods before every other pixel can be reached). This is also what
    // ChiSurf's `_flood` always did. Recorded difference on a 1024^2 basin
    // image with 200 markers: 13% of the pixels change basin between the two.
    for (size_t i = 0; i < marker_locations.size(); ++i) {
        const long long index = marker_locations[i];
        push(image[index], 0, index, index);
    }
    (void) neg_inf;

    long long age_counter = 1;
    while (size > 0) {
        const double value = heap_value[0];
        const long long index = heap_index[0];
        const long long source = heap_source[0];
        // sift the last element down into the hole at the root
        size -= 1;
        const double last_value = heap_value[size];
        const long long last_age = heap_age[size];
        const long long last_index = heap_index[size];
        const long long last_source = heap_source[size];
        size_t position = 0;
        while (true) {
            size_t child = 2 * position + 1;
            if (child >= size) break;
            if (child + 1 < size &&
                (heap_value[child + 1] < heap_value[child] ||
                 (heap_value[child + 1] == heap_value[child] &&
                  heap_age[child + 1] < heap_age[child])))
                child += 1;
            if (last_value < heap_value[child] ||
                (last_value == heap_value[child] && last_age <= heap_age[child]))
                break;
            heap_value[position] = heap_value[child];
            heap_age[position] = heap_age[child];
            heap_index[position] = heap_index[child];
            heap_source[position] = heap_source[child];
            position = child;
        }
        if (size > 0) {
            heap_value[position] = last_value;
            heap_age[position] = last_age;
            heap_index[position] = last_index;
            heap_source[position] = last_source;
        }

        for (size_t k = 0; k < offsets.size(); ++k) {
            const long long neighbour = index + offsets[k];
            if (!mask[neighbour]) continue;
            if (output[neighbour] != 0) continue;
            age_counter += 1;
            double neighbour_value = image[neighbour];
            if (neighbour_value < value) neighbour_value = value;
            output[neighbour] = output[index];
            push(neighbour_value, age_counter, neighbour, source);
        }
    }
}

}  // anonymous namespace

void watershed(
        const double* image, int n_rows, int n_cols,
        const long long* markers, int m_rows, int m_cols,
        const unsigned char* mask, int k_rows, int k_cols,
        int connectivity,
        long long** out_labels, int* out_rows, int* out_cols) {
    require(image != nullptr, "watershed", "image is null");
    require(markers != nullptr, "watershed", "markers is null");
    require(mask != nullptr, "watershed", "mask is null");
    require(n_rows >= 0 && n_cols >= 0, "watershed", "negative image extent");
    require(m_rows == n_rows && m_cols == n_cols, "watershed",
            "markers and image must have the same shape");
    require(k_rows == n_rows && k_cols == n_cols, "watershed",
            "mask and image must have the same shape");
    require(connectivity >= 1, "watershed", "connectivity must be at least 1");

    *out_labels = nullptr;
    *out_rows = n_rows;
    *out_cols = n_cols;
    if (n_rows == 0 || n_cols == 0) {
        *out_labels = new long long[0];
        return;
    }

    // generate_binary_structure(2, c) caps at the full 3x3, so anything at or
    // above 2 in two dimensions is the same neighbourhood; match that rather
    // than raising, which is what skimage's _validate_connectivity does.
    const int conn = connectivity >= 2 ? 2 : connectivity;

    // pad the three planes by one so the flood can use raw offsets: the
    // border is masked off, so a neighbour index can never leave the array.
    const int W = n_cols + 2;
    const size_t padded = static_cast<size_t>(n_rows + 2) * W;
    std::vector<double> padded_image(padded, 0.0);
    std::vector<unsigned char> padded_mask(padded, 0);
    std::vector<long long> padded_markers(padded, 0);
    for (int r = 0; r < n_rows; ++r) {
        for (int c = 0; c < n_cols; ++c) {
            const size_t src = static_cast<size_t>(r) * n_cols + c;
            const size_t dst = static_cast<size_t>(r + 1) * W + (c + 1);
            padded_image[dst] = image[src];
            const bool in = mask[src] != 0;
            padded_mask[dst] = in ? 1 : 0;
            // a marker outside the mask would flood nothing and, worse,
            // leave a label in the output that no pixel belongs to
            padded_markers[dst] = in ? markers[src] : 0;
        }
    }

    const std::vector<long long> offsets = neighbour_offsets(W, conn);

    std::vector<long long> marker_locations;
    marker_locations.reserve(16);
    for (size_t i = 0; i < padded; ++i)
        if (padded_markers[i] != 0) marker_locations.push_back(static_cast<long long>(i));

    long long* labels = new long long[padded];
    std::copy(padded_markers.begin(), padded_markers.end(), labels);
    if (!marker_locations.empty())
        flood(padded_image.data(), marker_locations, offsets, padded_mask.data(), labels);

    *out_labels = new long long[static_cast<size_t>(n_rows) * n_cols];
    for (int r = 0; r < n_rows; ++r)
        for (int c = 0; c < n_cols; ++c)
            (*out_labels)[static_cast<size_t>(r) * n_cols + c] =
                labels[static_cast<size_t>(r + 1) * W + (c + 1)];
    delete[] labels;
}

void marching_squares(
        const double* image, int n_rows, int n_cols,
        double level, int vertex_connect_high,
        double** out_segments, int* out_n_segments, int* out_n_cols) {
    require(image != nullptr, "marching_squares", "image is null");
    require(n_rows >= 2 && n_cols >= 2, "marching_squares",
            "image must be at least 2x2");

    // upper bound: every block can emit at most two segments
    const size_t capacity =
        2 * static_cast<size_t>(n_rows - 1) * (n_cols - 1);
    double* segments = new double[4 * capacity];
    size_t count = 0;

    const bool vch = vertex_connect_high != 0;
    for (int r0 = 0; r0 < n_rows - 1; ++r0) {
        for (int c0 = 0; c0 < n_cols - 1; ++c0) {
            const int r1 = r0 + 1, c1 = c0 + 1;
            const double ul = image[static_cast<size_t>(r0) * n_cols + c0];
            const double ur = image[static_cast<size_t>(r0) * n_cols + c1];
            const double ll = image[static_cast<size_t>(r1) * n_cols + c0];
            const double lr = image[static_cast<size_t>(r1) * n_cols + c1];
            if (std::isnan(ul) || std::isnan(ur) || std::isnan(ll) || std::isnan(lr))
                continue;

            int square_case = 0;
            if (ul > level) square_case += 1;
            if (ur > level) square_case += 2;
            if (ll > level) square_case += 4;
            if (lr > level) square_case += 8;
            if (square_case == 0 || square_case == 15) continue;

            const double top_r = r0;
            const double top_c = c0 + fraction(ul, ur, level);
            const double bottom_r = r1;
            const double bottom_c = c0 + fraction(ll, lr, level);
            const double left_r = r0 + fraction(ul, ll, level);
            const double left_c = c0;
            const double right_r = r0 + fraction(ur, lr, level);
            const double right_c = c1;

            // skimage's case table, in its exact emission order -- the order
            // is part of the contract because the caller assembles contours
            // by chaining endpoint pairs in this sequence.
            auto emit = [&](double a, double b, double c, double d) {
                segments[4 * count + 0] = a;
                segments[4 * count + 1] = b;
                segments[4 * count + 2] = c;
                segments[4 * count + 3] = d;
                count += 1;
            };
            switch (square_case) {
                case 1:  emit(top_r, top_c, left_r, left_c); break;
                case 2:  emit(right_r, right_c, top_r, top_c); break;
                case 3:  emit(right_r, right_c, left_r, left_c); break;
                case 4:  emit(left_r, left_c, bottom_r, bottom_c); break;
                case 5:  emit(top_r, top_c, bottom_r, bottom_c); break;
                case 6:
                    if (vch) {
                        emit(left_r, left_c, top_r, top_c);
                        emit(right_r, right_c, bottom_r, bottom_c);
                    } else {
                        emit(right_r, right_c, top_r, top_c);
                        emit(left_r, left_c, bottom_r, bottom_c);
                    }
                    break;
                case 7:  emit(right_r, right_c, bottom_r, bottom_c); break;
                case 8:  emit(bottom_r, bottom_c, right_r, right_c); break;
                case 9:
                    if (vch) {
                        emit(top_r, top_c, right_r, right_c);
                        emit(bottom_r, bottom_c, left_r, left_c);
                    } else {
                        emit(top_r, top_c, left_r, left_c);
                        emit(bottom_r, bottom_c, right_r, right_c);
                    }
                    break;
                case 10: emit(bottom_r, bottom_c, top_r, top_c); break;
                case 11: emit(bottom_r, bottom_c, left_r, left_c); break;
                case 12: emit(left_r, left_c, right_r, right_c); break;
                case 13: emit(top_r, top_c, right_r, right_c); break;
                case 14: emit(left_r, left_c, top_r, top_c); break;
                default: break;
            }
        }
    }

    *out_segments = segments;
    *out_n_segments = static_cast<int>(count);
    *out_n_cols = 4;
}

}  // namespace tttrlib

// ---- registry entries (Registry.h, core): declared next to the code, registered
// when this library loads; a static consumer links the archive whole.
namespace {
const char* const kRegionSegmentationEntry = R"JSON({
  "name": "region_segmentation",
  "label": "Watershed segmentation",
  "summary": "Marker-based watershed of an image (scikit-image-exact).",
  "description": "Vincent & Soille's immersion watershed from given markers, priority-flooded so it reproduces scikit-image bit for bit: every pixel is assigned to the marker whose basin it drains into. Region detection for FLIM/ICS masks and for the ndxplorer selection tools.",
  "operation_type": "region_detection",
  "method": "watershed",
  "params_schema": {
    "type": "object",
    "properties": {}
  },
  "inputs": {
    "required": [
      "image",
      "markers"
    ]
  },
  "outputs": {
    "columns": [
      "labels"
    ]
  },
  "row_grain": "pixel",
  "references": [
    {
      "type": "journal",
      "authors": "Vincent, L., Soille, P.",
      "title": "Watersheds in digital spaces: an efficient algorithm based on immersion simulations",
      "journal": "IEEE Trans Pattern Anal Mach Intell",
      "year": 1991,
      "volume": "13",
      "pages": "583-598"
    },
    {
      "type": "conference",
      "authors": "Lorensen, W. E., Cline, H. E.",
      "title": "Marching cubes: a high resolution 3D surface construction algorithm",
      "journal": "SIGGRAPH Comput Graph",
      "year": 1987,
      "volume": "21",
      "pages": "163-169"
    }
  ],
  "api": [
    "watershed"
  ],
  "can_replay": false
})JSON";
const char* const kIsoContoursEntry = R"JSON({
  "name": "iso_contours",
  "label": "Iso-contours by marching squares",
  "summary": "Contour segments of a 2-D image at a given level.",
  "description": "Lorensen & Cline's marching squares: the iso-level line segments of a 2-D image, returned as an (n, 4) array of segment endpoints. Reproduces scikit-image's find_contours geometry, including its vertex-connectivity convention.",
  "operation_type": "region_detection",
  "method": "marching_squares",
  "params_schema": {
    "type": "object",
    "properties": {
      "level": {
        "type": "number",
        "title": "Contour level"
      },
      "vertex_connect_high": {
        "type": "boolean",
        "title": "Connect high vertices",
        "default": false
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
      "contour_segments"
    ]
  },
  "row_grain": "pixel",
  "references": [
    {
      "type": "journal",
      "authors": "Vincent, L., Soille, P.",
      "title": "Watersheds in digital spaces: an efficient algorithm based on immersion simulations",
      "journal": "IEEE Trans Pattern Anal Mach Intell",
      "year": 1991,
      "volume": "13",
      "pages": "583-598"
    },
    {
      "type": "conference",
      "authors": "Lorensen, W. E., Cline, H. E.",
      "title": "Marching cubes: a high resolution 3D surface construction algorithm",
      "journal": "SIGGRAPH Comput Graph",
      "year": 1987,
      "volume": "21",
      "pages": "163-169"
    }
  ],
  "api": [
    "marching_squares"
  ],
  "can_replay": false
})JSON";
bool register_watershed_entries() {
    tttrlib::register_algorithm_json("math", "region_segmentation", kRegionSegmentationEntry);
    tttrlib::register_algorithm_json("math", "iso_contours", kIsoContoursEntry);
    return true;
}
const bool kWatershedRegistered = register_watershed_entries();
}  // namespace
