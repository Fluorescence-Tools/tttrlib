// SPDX-License-Identifier: BSD-3-Clause
#ifndef TTTRLIB_HISTOGRAM_H
#define TTTRLIB_HISTOGRAM_H

#include <algorithm>
#include <vector>   
#include <cstdio>
#include <string.h>
#include <string>

#include <map>
#include <cmath>

#include "HistogramAxis.h"

#include <atomic>
#include <thread>

/*!
 * Point count above which a histogram fill is worth threading.
 *
 * Below it the private per-thread buffers and the reduction cost more than the
 * fill saves. The figure is not tuned to a machine -- it is the order of
 * magnitude at which the fill stops being dominated by setup, and it matches
 * where boost-histogram's own users switch their `threads=` argument on.
 */
#ifndef TTTRLIB_HIST_PARALLEL_MIN_POINTS
#define TTTRLIB_HIST_PARALLEL_MIN_POINTS 200000
#endif

/*!
 * Largest histogram, in bins, that is filled with private per-thread buffers.
 *
 * Threading a fill costs one full copy of the histogram per thread. At 4M bins
 * and 8 threads that is 256 MB of scratch to save a few milliseconds, so past
 * this the fill stays serial -- which is also where the fill is memory-bound
 * anyway and threads buy least.
 */
#ifndef TTTRLIB_HIST_PARALLEL_MAX_BINS
#define TTTRLIB_HIST_PARALLEL_MAX_BINS 4194304
#endif


/*!
 * Total size of the private per-thread histograms, in bytes.
 *
 * This strategy -- a private copy per thread, summed at the end -- is the right
 * one while the copies stay small: the scatter parallelises perfectly and each
 * thread's copy is cache-resident. It stops being right when they do not.
 * boost-histogram never replicates at all; it batches 16384 points, computes
 * their bin indices in parallel, and scatters into ONE shared histogram
 * serially (see detail/fill_n.hpp, "Parallelization options" B). That costs a
 * serial scatter but no scratch, and it is why boost wins on a 1024x1024
 * histogram, where eight private copies are 64 MB.
 *
 * Rather than carry two fill strategies, the number of copies is bounded. Past
 * the budget the extra threads were not buying anything anyway: measured on an
 * 8-core Apple part, a 1024x1024 fill of ten million points took 31 ms with
 * four threads and 35 ms with eight.
 */
#ifndef TTTRLIB_HIST_PARALLEL_BUDGET_BYTES
#define TTTRLIB_HIST_PARALLEL_BUDGET_BYTES (32 << 20)
#endif


/*!
 * True when private per-thread copies would exceed the scratch budget, so the
 * partitioned strategy should be used instead. See histogram_partitioned_fill.
 */
inline bool histogram_should_partition(long long n_bins, unsigned n_threads) {
    if (n_threads < 2) return false;
    const long long scratch =
            (n_threads - 1) * n_bins * static_cast<long long>(sizeof(double));
    return scratch > TTTRLIB_HIST_PARALLEL_BUDGET_BYTES;
}


/*!
 * \brief How many threads to fill with, or 1 to stay on the serial path.
 *
 * \param n_requested 0 to decide from the data, 1 to force serial, or an
 *        explicit count. A caller that already knows how much of the machine it
 *        may use -- a GUI keeping a core free for the interface, a fit running
 *        histograms inside an outer parallel loop -- knows better than a
 *        heuristic here, and nesting two thread pools is worse than either.
 */
inline unsigned histogram_fill_threads(long long n_points, long long n_bins,
                                       int n_requested = 0) {
    if (n_requested == 1) return 1;
    if (n_requested > 1) return static_cast<unsigned>(n_requested);
    if (n_points < TTTRLIB_HIST_PARALLEL_MIN_POINTS) return 1;
    if (n_bins > TTTRLIB_HIST_PARALLEL_MAX_BINS) return 1;
    unsigned hw = std::thread::hardware_concurrency();
    if (hw < 2) return 1;
    // One chunk per thread, but never so many that a chunk is trivial.
    const long long max_useful = n_points / (TTTRLIB_HIST_PARALLEL_MIN_POINTS / 4);
    if (max_useful < 2) return 1;
    // No cap by histogram size: a histogram too large to replicate is filled by
    // partitioning instead of by fewer threads. See histogram_should_partition.
    (void) n_bins;
    const long long n = std::min<long long>(hw, max_useful);
    return n < 2 ? 1 : static_cast<unsigned>(n);
}


/*!
 * Fill `hist` by running `fill_chunk(begin, end, local)` on each of several
 * ranges in parallel, each into its own private histogram, then summing.
 *
 * Private buffers rather than atomics: the whole reason a fill is fast is that
 * the inner loop is a multiply and an add, and an atomic on every point undoes
 * exactly that.
 */
/*!
 * Points per unit of work handed to a thread.
 *
 * Small enough that a slow core cannot hold up the join by much, large enough
 * that claiming one is not the cost. See histogram_parallel_fill.
 */
#ifndef TTTRLIB_HIST_CHUNK_POINTS
#define TTTRLIB_HIST_CHUNK_POINTS 65536
#endif


/// Run `body(t)` on `n_threads` threads, t in [0, n_threads), and join.
template<typename Fn>
inline void histogram_run_threads(unsigned n_threads, Fn body) {
    std::vector<std::thread> workers;
    workers.reserve(n_threads - 1);
    for (unsigned t = 1; t < n_threads; t++) workers.emplace_back([&, t] { body(t); });
    body(0);
    for (auto& w : workers) w.join();
}


/*!
 * \brief Fill a LARGE histogram in parallel without replicating it.
 *
 * The private-copy strategy below is the right one while the copies stay
 * cache-resident, and the wrong one when they do not: eight private copies of a
 * 1024x1024 histogram are 64 MB, and obtaining, zeroing and summing them costs
 * more than the threading saves.
 *
 * boost-histogram's answer is to not replicate at all -- batch the points,
 * compute their bin indices in parallel, and scatter into one shared histogram
 * SERIALLY (detail/fill_n.hpp, "Parallelization options" B). That removes the
 * scratch but leaves the scatter, which is the expensive half, on one thread.
 *
 * The same file describes a better option C and does not implement it: partition
 * the indices so that each thread owns a disjoint set of bins, and then the
 * scatter parallelises with no synchronisation at all, because two threads can
 * never touch the same cell. That is what this does. Each thread's share of the
 * histogram is n_cells/n_threads, so for the sizes where replication fails the
 * scatter target is back to being cache-resident -- which is the whole reason
 * replication worked in the first place.
 *
 * The cost is that every point is written once more and read once more, to get
 * it into its owner's bucket. That is streaming traffic, and it buys turning a
 * cache-missing scatter into a cache-hitting one.
 *
 * \param cell_of maps a point index to a flat cell index, or -1 to drop it
 */
template<typename WeightFn, typename CellFn>
inline void histogram_partitioned_fill(
        double* hist, int n_cells, long long n_points, unsigned n_threads,
        WeightFn weight_of, bool use_weights, CellFn cell_of) {
    const unsigned owners = n_threads;
    const long long cells_per_owner =
            (static_cast<long long>(n_cells) + owners - 1) / owners;

    // Buckets are reused across batches, so the allocation happens once.
    std::vector<std::vector<int>> bucket_cells(
            static_cast<std::size_t>(n_threads) * owners);
    std::vector<std::vector<double>> bucket_weights(
            use_weights ? static_cast<std::size_t>(n_threads) * owners : 0);

    // Batched so the buckets stay bounded regardless of how many points there
    // are: a hundred million points would otherwise mean 400 MB of indices.
    const long long batch = 1LL << 22;
    for (long long start = 0; start < n_points; start += batch) {
        const long long stop = std::min(start + batch, n_points);
        const long long n = stop - start;

        histogram_run_threads(n_threads, [&](unsigned t) {
            for (unsigned o = 0; o < owners; o++) {
                const std::size_t k = static_cast<std::size_t>(t) * owners + o;
                bucket_cells[k].clear();
                if (use_weights) bucket_weights[k].clear();
            }
            // Hoisted: the bucket vectors for THIS thread are contiguous, so
            // the inner loop indexes an array rather than walking back into the
            // outer vector every point.
            std::vector<int>* my_cells = &bucket_cells[static_cast<std::size_t>(t) * owners];
            std::vector<double>* my_weights =
                    use_weights ? &bucket_weights[static_cast<std::size_t>(t) * owners] : nullptr;
            const long long a = start + (n * t) / n_threads;
            const long long b = start + (n * (t + 1)) / n_threads;
            for (long long i = a; i < b; i++) {
                const int c = cell_of(i);
                if (c < 0) continue;
                // Integer division, measured against a reciprocal multiply and
                // found no slower -- the loop is bound by the memory it touches,
                // not by this.
                unsigned o = static_cast<unsigned>(c / cells_per_owner);
                if (o >= owners) o = owners - 1;
                my_cells[o].push_back(c);
                if (use_weights) my_weights[o].push_back(weight_of(i));
            }
        });

        // Owner o writes only cells in its own range, so no two threads ever
        // touch the same cell and no synchronisation is needed.
        histogram_run_threads(n_threads, [&](unsigned o) {
            for (unsigned t = 0; t < n_threads; t++) {
                const std::size_t k = static_cast<std::size_t>(t) * owners + o;
                const std::vector<int>& cs = bucket_cells[k];
                if (use_weights) {
                    const std::vector<double>& ws = bucket_weights[k];
                    for (std::size_t j = 0; j < cs.size(); j++) hist[cs[j]] += ws[j];
                } else {
                    for (std::size_t j = 0; j < cs.size(); j++) hist[cs[j]] += 1.0;
                }
            }
        });
    }
}


/*!
 * Fill `hist` by running `fill_chunk(begin, end, out)` over the data in
 * parallel, each thread into its own private histogram, then summing.
 *
 * Private buffers rather than atomics: the whole reason a fill is fast is that
 * the inner loop is a multiply and an add, and an atomic on every point undoes
 * exactly that.
 *
 * Work is claimed from a shared counter rather than split into one equal chunk
 * per thread. Equal chunks assume equal cores, and on a machine with
 * performance and efficiency cores -- every recent laptop -- they are not:
 * every thread waits for the slowest, so the fill runs at efficiency-core
 * speed. Measured on an 8-core Apple part, a 512x512 fill of ten million points
 * took 10.9 ms with eight equal chunks and 8.3 ms with six, because two threads
 * fewer meant two efficiency cores fewer. Claiming chunks dynamically gets the
 * six-thread number out of eight threads without anybody tuning a constant, and
 * it degrades gracefully when the machine is busy with something else.
 */
template<typename Fn>
inline void histogram_parallel_fill(double* hist, int n_cells,
                                    long long n_points, unsigned n_threads,
                                    Fn fill_chunk) {
    // One allocation for all the private copies, not one per thread. At a
    // 512x512 histogram and eight threads this is 16 MB of scratch, and the
    // cost of obtaining and zeroing it is a measurable part of the fill.
    //
    // The first worker writes straight into the caller's histogram: nothing
    // else touches it until the joins, it already holds the right starting
    // values, and it saves both a copy's worth of zeroing and a whole pass of
    // the reduction.
    const unsigned n_private = n_threads - 1;
    std::vector<double> scratch(static_cast<std::size_t>(n_private) * n_cells, 0.0);
    auto partial = [&](unsigned t) {
        return t == 0 ? hist : scratch.data() + static_cast<std::size_t>(t - 1) * n_cells;
    };

    std::atomic<long long> next_chunk{0};
    const long long chunk = TTTRLIB_HIST_CHUNK_POINTS;
    auto worker = [&](unsigned t) {
        double* out = partial(t);
        for (;;) {
            const long long begin = next_chunk.fetch_add(chunk, std::memory_order_relaxed);
            if (begin >= n_points) break;
            fill_chunk(begin, std::min(begin + chunk, n_points), out);
        }
    };

    std::vector<std::thread> workers;
    workers.reserve(n_threads - 1);
    for (unsigned t = 1; t < n_threads; t++) {
        workers.emplace_back([&, t] { worker(t); });
    }
    worker(0);
    for (auto& w : workers) w.join();
    if (n_private == 0) return;

    // Reduce in parallel too, by bin range rather than by thread. For a large
    // 2D histogram this is not a rounding error: eight private copies of a
    // 512x512 histogram is 16 MB to walk, and doing it on one thread cost more
    // than the threaded fill saved. Each range is written by exactly one
    // thread, so no locking.
    auto reduce_range = [&](int begin, int end) {
        for (unsigned t = 0; t < n_private; t++) {
            const double* src = scratch.data() + static_cast<std::size_t>(t) * n_cells;
            for (int b = begin; b < end; b++) hist[b] += src[b];
        }
    };
    workers.clear();
    const int cell_chunk = (n_cells + static_cast<int>(n_threads) - 1) /
                           static_cast<int>(n_threads);
    for (unsigned t = 0; t + 1 < n_threads; t++) {
        const int begin = static_cast<int>(t) * cell_chunk;
        const int end = std::min(begin + cell_chunk, n_cells);
        if (begin >= end) continue;
        workers.emplace_back([&, begin, end] { reduce_range(begin, end); });
    }
    const int last = static_cast<int>(n_threads - 1) * cell_chunk;
    if (last < n_cells) reduce_range(last, n_cells);
    for (auto& w : workers) w.join();
}


template<class T>
class Histogram {

private:

    std::map<size_t , HistogramAxis<T>> axes;

    T* histogram = nullptr; // A 1D array of that contains the histogram
    int number_of_axis = 0;
    int n_total_bins = 0;
    size_t getAxisDimensions(){
        return axes.size();
    }

public:

    void update(T *data, int n_rows_data, int n_cols_data){
        int axis_index;
        int global_bin_idx;
        int global_bin_offset;
        int n_axis;
        int current_bin_idx, current_n_bins;
        HistogramAxis<T> *current_axis;
        T data_value;

        // update the axes
        for(const auto& p : axes){
            axes[p.first].update();
        }

        // initialize a new empty histogram
        // clear the memory of the old histogram
        if (histogram != nullptr) {
            free(histogram);
        }
        // 1. count the total number of bins
        n_total_bins = 1;
        n_axis = 0;
        for(const auto& p : axes){
            axis_index = p.first;
            n_axis += 1;
            n_total_bins *= axes[axis_index].getNumberOfBins();
        }
        // 2. fill the histogram with zeros
        histogram = (T*) malloc(sizeof(T) * (n_total_bins));
        for(global_bin_idx=0; global_bin_idx < n_total_bins; global_bin_idx++){
            histogram[global_bin_idx] = 0.0;
        }

        // Fill the histogram
        // Very instructive for multi-dimensional array indexing
        // https://eli.thegreenplace.net/2015/memory-layout-of-multi-dimensional-arrays/
        bool is_inside;
        for(int i_row=0; i_row<n_rows_data; i_row++){
            // in this loop the position within the 1D array is calculated
            global_bin_offset = 0;
            is_inside = true;
            for(const auto& p : axes){
                axis_index = p.first;
                current_axis = &axes[axis_index];

                data_value = data[i_row*n_axis + axis_index];
                current_bin_idx = current_axis->getBinIdx(data_value);
                current_n_bins = current_axis->getNumberOfBins();

                if( (current_bin_idx < 0) || (current_bin_idx >= current_n_bins) ){
                    is_inside = false;
                    break;
                }
                global_bin_offset = current_bin_idx + current_n_bins * global_bin_offset;
            }
            if(is_inside){
                histogram[global_bin_offset] += 1.0;
            }
        }
    }

    /**
     * @brief Gets the histogram as a raw pointer and size.
     * 
     * Allocates a copy of the internal data that Python can safely own and free.
     * The SWIG typemap will call free() on this pointer when the numpy array is garbage collected.
     *
     * @param hist Pointer to receive the data pointer (T*)
     * @param dim Pointer to receive the number of elements
     */
    void get_histogram(T** hist, int* dim){
        if (histogram == nullptr || n_total_bins <= 0) {
            *hist = nullptr;
            *dim = 0;
            return;
        }
        // Allocate a copy using malloc() so Python can safely free() it
        // This aligns with SWIG's ARGOUTVIEWM_ARRAY1 typemap
        *hist = (T*) malloc(sizeof(T) * n_total_bins);
        if (*hist != nullptr) {
            memcpy(*hist, histogram, sizeof(T) * n_total_bins);
        }
        *dim = n_total_bins;
    }

    void set_axis(size_t data_column, HistogramAxis<T> &new_axis){
        axes[data_column] = new_axis;
    }

    void set_axis(
            size_t data_column,
            std::string name,
            T begin, T end, int n_bins,
            std::string axis_type
            ){
        HistogramAxis<T> new_axis(name, begin, end, n_bins, axis_type);
        set_axis(data_column, new_axis);
    }

    HistogramAxis<T> get_axis(size_t axis_index){
        return axes[axis_index];
    }

    Histogram() = default;

    virtual ~Histogram() {
        if (histogram != nullptr) {
            free(histogram);
            histogram = nullptr;
        }
    }

};


void bincount1D(int *data, int n_data, int *bins, int n_bins);


template<typename T>
void histogram1D(
        T *data, int n_data,
        double *weights, int n_weights,
        T *bin_edges, int n_bins,
        double *hist, int n_hist,
        const char *axis_type,
        bool use_weights,
        int n_threads_requested = 0
) {
    // The axis is worked out once here rather than per value. It used to be per
    // value, which meant two std::log10 calls on the same two constants for
    // every photon binned.
    //
    // The bound is also now `< n_bins` rather than `<= n_bins`. It was a heap
    // buffer overflow: a value one bin past the top edge produced index n_bins,
    // and callers size `hist` to exactly n_bins.
    const HistogramBinning<T> axis(bin_edges, n_bins, axis_type);
    const int limit = std::min(n_bins, n_hist);
    if (limit <= 0) return;

    const unsigned n_threads = histogram_fill_threads(n_data, limit, n_threads_requested);
    auto fill_chunk = [&](long long begin, long long end, double* out) {
        for (long long i = begin; i < end; i++) {
            const int bin_idx = axis.bin_of(data[i]);
            if (bin_idx >= 0 && bin_idx < limit) {
                out[bin_idx] += (use_weights) ? weights[i] : 1;
            }
        }
    };
    if (n_threads > 1) {
        if (histogram_should_partition(limit, n_threads)) {
            histogram_partitioned_fill(
                    hist, limit, n_data, n_threads,
                    [weights](long long i) { return weights[i]; }, use_weights,
                    [&](long long i) { return axis.bin_of(data[i]); });
        } else {
            histogram_parallel_fill(hist, limit, n_data, n_threads, fill_chunk);
        }
        return;
    }

    for (int i = 0; i < n_data; i++) {
        const int bin_idx = axis.bin_of(data[i]);
        if (bin_idx >= 0 && bin_idx < limit) {
            hist[bin_idx] += (use_weights) ? weights[i] : 1;
        }
    }
}


/*!
 * \brief Two-dimensional histogram of paired values.
 *
 * Each axis is binned by the same HistogramBinning that histogram1D uses, so
 * the two cannot drift apart. The axes are independent:
 * the two may use different bin counts and different axis types, so a
 * lifetime-versus-intensity plot can be linear in one and logarithmic in the
 * other without the caller pre-transforming anything.
 *
 * The output is row-major with x as the slow axis, i.e. `hist[i * n_bins_y + j]`
 * is the count for x-bin i and y-bin j. That is what numpy reshapes to
 * `(n_bins_x, n_bins_y)` without a copy.
 *
 * Pairs are dropped when EITHER coordinate falls outside its axis. Clamping
 * them to the edge bins instead would pile everything outside the range onto
 * the border, which reads as structure that is not in the data.
 *
 * @tparam T value type of the two data arrays
 * @param data_x, n_data_x first coordinate of each pair
 * @param data_y, n_data_y second coordinate; must be the same length as data_x
 * @param weights, n_weights per-pair weights, used only when use_weights is true
 * @param bin_edges_x, n_bins_x bin edges of the x axis, in ascending order
 * @param bin_edges_y, n_bins_y bin edges of the y axis, in ascending order
 * @param hist, n_hist output, n_bins_x * n_bins_y entries, added to (not cleared)
 * @param axis_type_x, axis_type_y "lin", "log10", or anything else for a search
 *        over arbitrary edges
 * @param use_weights add weights[i] instead of 1
 */
template<typename T>
void histogram2D(
        T *data_x, int n_data_x,
        T *data_y, int n_data_y,
        double *weights, int n_weights,
        T *bin_edges_x, int n_bins_x,
        T *bin_edges_y, int n_bins_y,
        double *hist, int n_hist,
        const char *axis_type_x,
        const char *axis_type_y,
        bool use_weights,
        int n_threads_requested = 0
) {
    // A shorter y array would otherwise be read past its end for every pair
    // beyond it -- silently, and with plausible-looking output.
    const int n = std::min(n_data_x, n_data_y);
    if (n_hist < n_bins_x * n_bins_y) return;

    const HistogramBinning<T> ax(bin_edges_x, n_bins_x, axis_type_x);
    const HistogramBinning<T> ay(bin_edges_y, n_bins_y, axis_type_y);
    const int n_cells = n_bins_x * n_bins_y;

    const unsigned n_threads = histogram_fill_threads(n, n_cells, n_threads_requested);
    auto fill_chunk = [&](long long begin, long long end, double* out) {
        for (long long i = begin; i < end; i++) {
            const int ix = ax.bin_of(data_x[i]);
            if (ix < 0) continue;
            const int iy = ay.bin_of(data_y[i]);
            if (iy < 0) continue;
            out[ix * n_bins_y + iy] +=
                    (use_weights && i < n_weights) ? weights[i] : 1;
        }
    };
    if (n_threads > 1) {
        if (histogram_should_partition(n_cells, n_threads)) {
            histogram_partitioned_fill(
                    hist, n_cells, n, n_threads,
                    [weights](long long i) { return weights[i]; },
                    use_weights && n_weights >= n,
                    [&](long long i) {
                        const int ix = ax.bin_of(data_x[i]);
                        if (ix < 0) return -1;
                        const int iy = ay.bin_of(data_y[i]);
                        if (iy < 0) return -1;
                        return ix * n_bins_y + iy;
                    });
        } else {
            histogram_parallel_fill(hist, n_cells, n, n_threads, fill_chunk);
        }
        return;
    }

    for (int i = 0; i < n; i++) {
        const int ix = ax.bin_of(data_x[i]);
        if (ix < 0) continue;
        const int iy = ay.bin_of(data_y[i]);
        if (iy < 0) continue;
        hist[ix * n_bins_y + iy] +=
                (use_weights && i < n_weights) ? weights[i] : 1;
    }
}



/*!
 * \brief histogram1D over a range, without materialising the bin edges.
 *
 * A caller that has "64 bins from 0 to 100" -- which is what a plotting front
 * end has -- should not have to build and pass a 64-element array to say so.
 * The edges are implied, and for a logarithmic axis they are geometric.
 *
 * The bins match the edge-array form exactly: bin i starts at
 * `lo + i * (hi - lo) / (n_bins - 1)`, so the two can be mixed without a shift.
 *
 * \param lo, hi first and last bin start
 * \param log geometric rather than linear spacing
 */
template<typename T>
void histogram1D_range(
        T *data, int n_data,
        double *weights, int n_weights,
        double lo, double hi, int n_bins,
        double *hist, int n_hist,
        bool log,
        bool use_weights,
        int n_threads_requested = 0
) {
    std::vector<T> edges(n_bins > 0 ? n_bins : 0);
    make_bin_edges(edges.data(), n_bins, lo, hi, log);
    histogram1D(data, n_data, weights, n_weights, edges.data(), n_bins,
                hist, n_hist, log ? "log10" : "lin", use_weights,
                n_threads_requested);
}


/*!
 * \brief histogram2D over two ranges, without materialising the bin edges.
 *
 * \see histogram1D_range
 */
template<typename T>
void histogram2D_range(
        T *data_x, int n_data_x,
        T *data_y, int n_data_y,
        double *weights, int n_weights,
        double lo_x, double hi_x, int n_bins_x,
        double lo_y, double hi_y, int n_bins_y,
        double *hist, int n_hist,
        bool log_x, bool log_y,
        bool use_weights,
        int n_threads_requested = 0
) {
    std::vector<T> ex(n_bins_x > 0 ? n_bins_x : 0);
    std::vector<T> ey(n_bins_y > 0 ? n_bins_y : 0);
    make_bin_edges(ex.data(), n_bins_x, lo_x, hi_x, log_x);
    make_bin_edges(ey.data(), n_bins_y, lo_y, hi_y, log_y);
    histogram2D(data_x, n_data_x, data_y, n_data_y, weights, n_weights,
                ex.data(), n_bins_x, ey.data(), n_bins_y, hist, n_hist,
                log_x ? "log10" : "lin", log_y ? "log10" : "lin",
                use_weights, n_threads_requested);
}


#endif //TTTRLIB_HISTOGRAM_H
