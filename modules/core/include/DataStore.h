// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_DATASTORE_H
#define TTTRLIB_DATASTORE_H

/*!
 * \file DataStore.h
 * \brief The columnar table, and the histogram fills over it.
 *
 * The DataStore itself -- Column, BitMask, the registry, the expression engine
 * that gates rows -- lives in **ptolib** (`thirdparty/ptolib/ptolib.h`,
 * https://github.com/tpeulen/ptolib), the header tttrlib shares with IMP.bff so
 * a table written by one is the table the other reads. This header re-exports
 * those types under `tttrlib::data`, where every caller and every binding has
 * always found them, and adds the one thing that is tttrlib's: filling a
 * `hist::HistogramNd` from columns.
 *
 * The implementation is compiled once, in `src/DataStore.cpp`
 * (`PTOLIB_IMPLEMENTATION`); everything else includes this header.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <vector>

#include "ptolib/ptolib.h"
#include "HistogramNd.h"

namespace tttrlib {
namespace data {

using pto::DefaultInitAllocator;
using pto::RawVector;
using pto::ColumnType;
using pto::is_floating;
using pto::column_type_name;
using pto::column_type_size;
using pto::metadata_to_msgpack;
using pto::metadata_from_msgpack;
using pto::NaRange;
using pto::BitMask;
using pto::Column;
using pto::DataStoreInfo;
using pto::DataStore;
using pto::DataStoreRegistry;
using pto::live_data_stores;
using pto::live_data_store_bytes;
using pto::ExprScalarType;
using pto::ExprColumn;
using pto::ExpressionEngine;

/*!
 * \brief Fill a histogram from store columns.
 *
 * The columns keep their own types -- a float32 column is read as float32, a
 * dictionary-encoded string column as its codes -- because the fill is
 * templated on the accessor. Nothing is converted to double first, so a
 * ten-million-row float32 column does not become an 80 MB temporary on the way
 * into a plot.
 *
 * A row is counted only if the store's selection allows it AND every column
 * involved says the value is valid. That is one rule covering both "this burst
 * is not in the current selection" and "this quantity was not measured here",
 * and it means neither can be mistaken for data that merely fell off the axis.
 *
 * \param h        the histogram, whose rank must equal the number of columns
 * \param store    the table the columns come from
 * \param columns  one column index per axis
 * \param weight   a column to weight by, or -1
 * \param n_threads 0 to decide, 1 to force serial, or an explicit count
 */
inline void fill_histogram(hist::HistogramNd& h, const DataStore& store,
                           const std::vector<int>& columns, int weight = -1,
                           int n_threads = 0) {
    if (static_cast<int>(columns.size()) != h.rank())
        throw std::invalid_argument("fill_histogram: one column per axis is required");

    std::vector<const Column*> cols;
    cols.reserve(columns.size());
    for (int i : columns) cols.push_back(&store.column(i));
    const Column* w = (weight >= 0) ? &store.column(weight) : nullptr;

    const Column* const* cp = cols.data();
    const DataStore* sp = &store;
    const std::size_t n_cols = cols.size();

    h.fill_with(
            [cp](int d, long long i) { return cp[d]->value_at(static_cast<std::size_t>(i)); },
            [cp, sp, n_cols, w](long long i) {
                const std::size_t r = static_cast<std::size_t>(i);
                if (!sp->row_selected(r)) return false;
                for (std::size_t d = 0; d < n_cols; d++)
                    if (!cp[d]->valid(r)) return false;
                if (w != nullptr && !w->valid(r)) return false;
                return true;
            },
            // The weight is read out of its column, in its own type, exactly
            // like a coordinate. Materialising it as a double array first --
            // which is what this did -- allocated and filled one double per ROW
            // per fill: 14 MB on a two-million-row table, three times per
            // redraw, to widen values that are added up and discarded.
            [w](long long i) {
                // Guarded rather than relying on the caller never asking: the
                // guard is one predictable branch, and a null dereference here
                // would be a crash in a fill loop.
                return w != nullptr ? w->value_at(static_cast<std::size_t>(i)) : 1.0;
            },
            w != nullptr,
            static_cast<long long>(store.n_rows()), n_threads);
}

/*!
 * \brief Fill a Mean or WeightedMean histogram with a SAMPLE column, binned by
 *        the coordinate columns.
 *
 * The parameter map: each bin holds the mean of `sample` over the rows that
 * landed in it, rather than how many landed there. Two axis columns and a
 * lifetime column make a lifetime image; the same call with one axis makes a
 * profile along it.
 *
 * A row is used only if the selection allows it and every column involved --
 * each axis, the sample, and the weight -- says its value is valid. The sample
 * carries a stricter rule than the axes do: a non-finite sample is skipped even
 * if the column does not mark it missing, because Welford's update is recursive
 * and a single NaN leaves the bin NaN for every row after it. Dropping the row
 * loses one measurement; keeping it loses the pixel.
 *
 * \param h         the histogram; its storage must be Mean or WeightedMean
 * \param store     the table the columns come from
 * \param columns   one column index per axis
 * \param sample    the column being averaged
 * \param weight    a column to weight by, or -1 (WeightedMean only)
 */
inline void fill_histogram_sample(hist::HistogramNd& h, const DataStore& store,
                                  const std::vector<int>& columns, int sample,
                                  int weight = -1) {
    if (static_cast<int>(columns.size()) != h.rank())
        throw std::invalid_argument(
                "fill_histogram_sample: one column per axis is required");

    std::vector<const Column*> cols;
    cols.reserve(columns.size());
    for (int i : columns) cols.push_back(&store.column(i));
    const Column& s = store.column(sample);
    const Column* w = (weight >= 0) ? &store.column(weight) : nullptr;

    std::vector<double> weights;
    if (w != nullptr) {
        weights.resize(store.n_rows());
        for (std::size_t i = 0; i < weights.size(); i++) weights[i] = w->value_at(i);
    }

    const Column* const* cp = cols.data();
    const Column* sp_col = &s;
    const DataStore* sp = &store;
    const std::size_t n_cols = cols.size();

    h.fill_sample_with(
            [cp](int d, long long i) { return cp[d]->value_at(static_cast<std::size_t>(i)); },
            [cp, sp, sp_col, n_cols, w](long long i) {
                const std::size_t r = static_cast<std::size_t>(i);
                if (!sp->row_selected(r)) return false;
                for (std::size_t d = 0; d < n_cols; d++)
                    if (!cp[d]->valid(r)) return false;
                if (!sp_col->valid(r)) return false;
                if (!std::isfinite(sp_col->value_at(r))) return false;
                if (w != nullptr && !w->valid(r)) return false;
                return true;
            },
            [sp_col](long long i) { return sp_col->value_at(static_cast<std::size_t>(i)); },
            static_cast<long long>(store.n_rows()),
            weights.empty() ? nullptr : weights.data());
}

/*!
 * \brief A category axis over a string column's dictionary.
 *
 * The bridge that lets a text column be histogrammed without the strings ever
 * reaching the histogram: the axis bins the integer codes, and the labels come
 * back from the dictionary for the tick marks.
 */
inline hist::Axis category_axis_for(const Column& c) {
    if (c.type() != ColumnType::String)
        throw std::invalid_argument("category_axis_for: not a string column");
    std::vector<int> codes(c.dictionary().size());
    for (std::size_t i = 0; i < codes.size(); i++) codes[i] = static_cast<int>(i);
    return hist::Axis::category(codes.data(), static_cast<int>(codes.size()),
                                hist::AxisOptions(), c.name());
}

}  // namespace data
}  // namespace tttrlib

#endif  // TTTRLIB_DATASTORE_H
