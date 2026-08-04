// SPDX-License-Identifier: BSD-3-Clause
package io.github.fluorescencetools.tttrlib.imagej.core;

import io.github.fluorescencetools.tttrlib.CLSMImage;
import io.github.fluorescencetools.tttrlib.CLSMSettings;
import io.github.fluorescencetools.tttrlib.DecayFitNExp;
import io.github.fluorescencetools.tttrlib.DecayFitNExpOptions;
import io.github.fluorescencetools.tttrlib.DecayFitNExpResult;
import io.github.fluorescencetools.tttrlib.NativeLoader;
import io.github.fluorescencetools.tttrlib.TTTR;
import io.github.fluorescencetools.tttrlib.VectorDouble;
import io.github.fluorescencetools.tttrlib.VectorInt32;

import java.util.stream.IntStream;

/**
 * Multi-exponential decay fitting, one fit per (optionally binned) pixel.
 *
 * <p>Photon budget is the limiting factor: a single pixel of a confocal FLIM
 * frame typically holds tens of photons, far too few for a stable
 * multi-exponential fit. Frame stacking, micro-time coarsening and spatial
 * binning are therefore the normal operating point rather than refinements.</p>
 *
 * <p>Without an instrument response the fit runs against a delta IRF, i.e. it
 * does no deconvolution — the same arrangement the library's own decay-fit tests
 * use. Recovered lifetimes are then biased by the finite width of the real IRF,
 * so supply one when the absolute value matters.</p>
 */
public final class PerPixelFit {

    static { NativeLoader.load(); }

    private PerPixelFit() { }

    public static final class Params {
        public String path;
        public int[] channels;
        /** Instrument response, or {@code null} for a delta IRF (no deconvolution). */
        public double[] irf;
        public int nExponentials = 1;
        /** Combine {@code binning x binning} pixels before fitting. */
        public int binning = 4;
        public int microTimeCoarsening = 16;
        /** Skip pixels with fewer photons than this. */
        public int minPhotons = 100;
        public boolean stackFrames = true;
        /** Micro-time bin width in nanoseconds; taken from the file when <= 0. */
        public double dtNanoseconds = -1.0;
    }

    public static final class Result {
        /** Output grid after binning. */
        public int nLines, nPixel;
        /** Per-component lifetime maps, {@code [component][y * nPixel + x]}. */
        public double[][] lifetimes;
        /** Per-component amplitude maps, same layout. */
        public double[][] amplitudes;
        /** Negative log-likelihood per pixel; NaN where no fit was attempted. */
        public double[] logLikelihood;
        /** Photons that entered each fit. */
        public double[] photons;
        public int fitted, skipped;
        public double dtNanoseconds;
    }

    public static Result fit(Params p) {
        final TTTR tttr = new TTTR(p.path);
        final VectorInt32 ch = new VectorInt32();
        if (p.channels != null) for (int c : p.channels) ch.add(c);
        final CLSMImage img = new CLSMImage(tttr, new CLSMSettings(), null, true, ch);

        final int nFrames = img.getN_frames();
        final int nLines = img.getN_lines();
        final int nPixel = img.getN_pixel();
        if (nFrames <= 0 || nLines <= 0 || nPixel <= 0) {
            throw new ClsmReconstructor.NoImageException(
                    "No CLSM image could be reconstructed (no scan markers?).");
        }

        final int coarsening = Math.max(1, p.microTimeCoarsening);
        // Probe for the true size first: n_tac depends on the coarsening and on
        // the hardware TAC range, so it cannot be predicted here.
        final int total = img.get_fluorescence_decay_into(new int[0], coarsening,
                p.stackFrames, -1);
        final int outFrames = p.stackFrames ? 1 : nFrames;
        final long pixels = (long) outFrames * nLines * nPixel;
        if (pixels <= 0 || total <= 0) {
            throw new IllegalStateException("no decay data");
        }
        final int nTac = (int) (total / pixels);
        final int[] decays = new int[total];
        img.get_fluorescence_decay_into(decays, coarsening, p.stackFrames, -1);

        final int bin = Math.max(1, p.binning);
        final Result r = new Result();
        r.nLines = (nLines + bin - 1) / bin;
        r.nPixel = (nPixel + bin - 1) / bin;
        r.dtNanoseconds = p.dtNanoseconds > 0
                ? p.dtNanoseconds
                : tttr.get_micro_time_resolution_s() * 1e9 * coarsening;

        final int nOut = r.nLines * r.nPixel;
        final int nExp = Math.max(1, p.nExponentials);
        r.lifetimes = new double[nExp][nOut];
        r.amplitudes = new double[nExp][nOut];
        r.logLikelihood = new double[nOut];
        r.photons = new double[nOut];
        java.util.Arrays.fill(r.logLikelihood, Double.NaN);

        final double[] irf = new double[nTac];
        if (p.irf != null && p.irf.length > 0) {
            final int n = Math.min(irf.length, p.irf.length);
            System.arraycopy(p.irf, 0, irf, 0, n);
        } else {
            irf[0] = 1.0;   // delta IRF: fit without deconvolution
        }
        final double[] background = new double[nTac];

        final java.util.concurrent.atomic.AtomicInteger fitted =
                new java.util.concurrent.atomic.AtomicInteger();
        final java.util.concurrent.atomic.AtomicInteger skipped =
                new java.util.concurrent.atomic.AtomicInteger();

        // One fit per output pixel, in parallel. Each task keeps its own buffers;
        // the native fit call itself is independent per pixel.
        IntStream.range(0, nOut).parallel().forEach(idx -> {
            final int by = idx / r.nPixel, bx = idx % r.nPixel;
            final double[] data = new double[nTac];
            double count = 0;

            for (int f = 0; f < outFrames; f++) {
                for (int y = by * bin; y < Math.min((by + 1) * bin, nLines); y++) {
                    for (int x = bx * bin; x < Math.min((bx + 1) * bin, nPixel); x++) {
                        final int base = (((f * nLines) + y) * nPixel + x) * nTac;
                        for (int t = 0; t < nTac; t++) {
                            final int v = decays[base + t];
                            data[t] += v;
                            count += v;
                        }
                    }
                }
            }
            if (count < p.minPhotons) {
                skipped.incrementAndGet();
                return;
            }

            final DecayFitNExpOptions opts = new DecayFitNExpOptions();
            opts.setDt(r.dtNanoseconds);

            final double[] tau0 = new double[nExp];
            final double[] amp0 = new double[nExp];
            final int[] fixed = new int[nExp];
            for (int k = 0; k < nExp; k++) {
                // Spread the seeds so components do not start degenerate.
                tau0[k] = (k + 1) * nTac * r.dtNanoseconds / (4.0 * nExp);
                amp0[k] = 1.0 / nExp;
            }

            try {
                final DecayFitNExpResult res = DecayFitNExp.fit_buffers(
                        data, irf, background, tau0, amp0, fixed, opts);
                final VectorDouble lt = res.getLifetimes();
                final VectorDouble am = res.getAmplitudes();
                for (int k = 0; k < nExp; k++) {
                    if (k < lt.size()) r.lifetimes[k][idx] = lt.get(k);
                    if (k < am.size()) r.amplitudes[k][idx] = am.get(k);
                }
                r.logLikelihood[idx] = res.getNegative_log_likelihood();
                r.photons[idx] = res.getPhoton_count();
                fitted.incrementAndGet();
            } catch (Throwable t) {
                // A pixel that will not converge must not abort the image.
                skipped.incrementAndGet();
            }
        });

        r.fitted = fitted.get();
        r.skipped = skipped.get();
        return r;
    }
}
