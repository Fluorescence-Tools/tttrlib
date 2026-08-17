"""A/B of the CLSM reconstruction against an independent NumPy reconstruction.

`CLSMImage` turns a marker-annotated TTTR stream into frames, lines and pixels
and then histograms photons per pixel, per micro-time channel, and as moments
(mean arrival time, moment-estimator lifetime). None of that has an external
library implementation to compare with, so the reference here is a
from-the-markers NumPy reconstruction written from the documented conventions
and nothing else:

* the *default* routine (`test/python/clsm/test_CLSM_01.py`'s HT3 parameters):
  a frame starts at a marker event whose routing channel is in
  ``marker_frame_start``; a line is the event-index range between a
  ``marker_line_start`` and the next ``marker_line_stop`` marker (all with
  ``marker_event_type``); the pixel dwell is the *integer* quotient
  ``(t_stop - t_start) // n_pixel`` and a photon lands in pixel
  ``(t - t_start) // dwell`` if that is ``< n_pixel``, else it is dropped;
* the SP5 routine: frame markers on routing channels {4, 6} regardless of
  event type; inside a frame the line edges are the frame start followed by
  every marker on channels 1/2 in stream order, paired consecutively.

From that per-photon assignment everything downstream is a NumPy reduction:
`np.bincount` for the intensity image, `np.histogram` per pixel for the decays,
sums of micro-times for the mean-arrival image and ``(m1/m0 - m1_irf/m0_irf)*dt``
for the moment lifetime. Agreement is exact (counts) or to round-off (means).

`compute_ics` is already pinned to a NumPy FFT correlation in
`test_clsm_ics.py` (`test_autocorrelation_matches_numpy` etc.); it is not
repeated here.
"""
import os
import sys
import unittest

import numpy as np
import tttrlib

sys.path.insert(0, os.path.join(os.path.dirname(__file__), ".."))
from test_settings import settings, DATA_AVAILABLE  # noqa: E402

HT3 = settings["clsm_ht3_sample1_filename"]
SP5 = settings["clsm_sp5_filename"]

HT3_PARAMS = dict(
    marker_frame_start=[4], marker_line_start=1, marker_line_stop=2,
    marker_event_type=1, n_pixel_per_line=256, reading_routine="default",
    skip_before_first_frame_marker=True,
)


def _reference_assignment(tttr, frame_markers, line_start, line_stop,
                          marker_event_type, n_pixel, channels,
                          frame_marker_any_event_type=False,
                          leading_line_edge=False, n_lines_cap=None):
    """Return (frame, line, pixel, event_index) arrays for every photon that
    the documented convention places inside a pixel, and the frame count."""
    mt = np.asarray(tttr.macro_times, dtype=np.int64)
    ch = np.asarray(tttr.routing_channels)
    et = np.asarray(tttr.event_types)
    is_marker = et == marker_event_type
    if frame_marker_any_event_type:
        fs = np.where(np.isin(ch, frame_markers))[0]
    else:
        fs = np.where(is_marker & np.isin(ch, frame_markers))[0]
    ls = np.where(is_marker & (ch == line_start))[0]
    le = np.where(is_marker & (ch == line_stop))[0]
    # pair every line start with the next line stop after it
    stop_after = np.searchsorted(le, ls, side="right")
    keep = stop_after < len(le)
    ls, le = ls[keep], le[stop_after[keep]]
    frame_of_line = np.searchsorted(fs, ls, side="right") - 1
    ph = np.where((et == 0) & np.isin(ch, channels))[0]
    out_f, out_l, out_p, out_i = [], [], [], []
    lines_in_frame = np.zeros(len(fs), dtype=int)
    for k in range(len(ls)):
        f = frame_of_line[k]
        if f < 0:
            continue
        l = lines_in_frame[f]
        lines_in_frame[f] += 1
        if n_lines_cap is not None and l >= n_lines_cap:
            continue
        t0, t1 = mt[ls[k]], mt[le[k]]
        dwell = (t1 - t0) // n_pixel
        if dwell <= 0:
            continue
        a = np.searchsorted(ph, ls[k], side="left")
        b = np.searchsorted(ph, le[k], side="left")
        idx = ph[a:b]
        pix = (mt[idx] - t0) // dwell
        ok = pix < n_pixel
        out_f.append(np.full(ok.sum(), f))
        out_l.append(np.full(ok.sum(), l))
        out_p.append(pix[ok])
        out_i.append(idx[ok])
    cat = lambda xs: np.concatenate(xs) if xs else np.zeros(0, dtype=np.int64)
    return cat(out_f), cat(out_l), cat(out_p), cat(out_i), len(fs)


@unittest.skipIf(not DATA_AVAILABLE or not os.path.exists(HT3),
                 "CLSM HT3 sample not available")
class TestDefaultRoutineAgainstNumpy(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.tttr = tttrlib.TTTR(HT3)
        cls.img = tttrlib.CLSMImage(tttr_data=cls.tttr, **HT3_PARAMS)
        cls.img.fill(tttr_data=cls.tttr, channels=[0])
        f, l, p, i, nf = _reference_assignment(
            cls.tttr, [4], 1, 2, 1, 256, [0], n_lines_cap=256)
        cls.ref = (f, l, p, i)
        cls.n_frames_ref = nf

    def _ref_counts(self, n_frames, n_lines, n_pixel):
        f, l, p, _ = self.ref
        flat = np.zeros(n_frames * n_lines * n_pixel, dtype=np.int64)
        sel = f < n_frames
        np.add.at(flat, (f[sel] * n_lines + l[sel]) * n_pixel + p[sel], 1)
        return flat.reshape(n_frames, n_lines, n_pixel)

    def test_intensity_image_is_the_marker_reconstruction(self):
        img = self.img
        I = np.asarray(img.intensity, dtype=np.int64)
        self.assertEqual(I.shape, (img.n_frames, img.n_lines, img.n_pixel))
        ref = self._ref_counts(*I.shape)
        # the last, incomplete frame is not part of the image
        self.assertGreaterEqual(self.n_frames_ref, img.n_frames)
        self.assertEqual(int(np.abs(I - ref).sum()), 0)
        self.assertGreater(int(I.sum()), 1_000_000)  # not a vacuous zero image

    def test_tttr_indices_of_the_pixels_are_the_reference_photons(self):
        f, l, p, i = self.ref
        sel = f < self.img.n_frames
        got = np.sort(np.asarray(self.img.get_tttr_indices()))
        np.testing.assert_array_equal(got, np.sort(i[sel]))

    def test_fluorescence_decay_is_the_per_pixel_micro_time_histogram(self):
        img, tttr = self.img, self.tttr
        micro = np.asarray(tttr.micro_times, dtype=np.int64)
        coarsen = 64
        dec = np.asarray(img.get_fluorescence_decay(
            tttr, micro_time_coarsening=coarsen, stack_frames=True))
        self.assertEqual(dec.shape[:3], (1, img.n_lines, img.n_pixel))
        n_tac = dec.shape[3]
        f, l, p, i = self.ref
        sel = f < img.n_frames
        tac = micro[i[sel]] // coarsen
        ok = tac < n_tac
        ref = np.zeros((img.n_lines, img.n_pixel, n_tac), dtype=np.int64)
        np.add.at(ref, (l[sel][ok], p[sel][ok], tac[ok]), 1)
        # uint8 output saturates at 255; compare where the reference is below it
        got = dec[0].astype(np.int64)
        below = ref < 255
        self.assertTrue(np.all(got[below] == ref[below]))
        self.assertTrue(np.all(got[~below] == 255))
        self.assertGreater(int(ref.sum()), 1_000_000)

    def test_decay_of_masked_pixels_is_the_summed_histogram(self):
        img, tttr = self.img, self.tttr
        micro = np.asarray(tttr.micro_times, dtype=np.int64)
        rng = np.random.default_rng(3)
        mask = (rng.random((img.n_frames, img.n_lines, img.n_pixel)) < 0.3)
        mask = mask.astype(np.uint8)
        coarsen = 8
        got = np.asarray(img.get_decay_of_pixels(
            tttr, mask, tac_coarsening=coarsen, stack_frames=True))
        self.assertEqual(got.shape[0], 1)
        n_tac = got.shape[1]
        f, l, p, i = self.ref
        sel = (f < img.n_frames)
        inmask = mask[f[sel], l[sel], p[sel]].astype(bool)
        tac = micro[i[sel]][inmask] // coarsen
        ref = np.bincount(tac[tac < n_tac], minlength=n_tac)
        np.testing.assert_array_equal(got[0].astype(np.int64), ref)
        # and per frame
        got_f = np.asarray(img.get_decay_of_pixels(
            tttr, mask, tac_coarsening=coarsen, stack_frames=False))
        self.assertEqual(got_f.shape[0], img.n_frames)
        for fr in (0, img.n_frames // 2, img.n_frames - 1):
            s = sel & (f == fr)
            m = mask[f[s], l[s], p[s]].astype(bool)
            t = micro[i[s]][m] // coarsen
            np.testing.assert_array_equal(
                got_f[fr].astype(np.int64),
                np.bincount(t[t < n_tac], minlength=n_tac))

    def test_mean_micro_time_image_is_the_photon_average(self):
        img, tttr = self.img, self.tttr
        micro = np.asarray(tttr.micro_times, dtype=np.float64)
        res = tttr.header.micro_time_resolution
        n_min = 5
        got = np.asarray(img.get_mean_micro_time(
            tttr, minimum_number_of_photons=n_min, stack_frames=False))
        f, l, p, i = self.ref
        sel = f < img.n_frames
        shape = (img.n_frames, img.n_lines, img.n_pixel)
        flat = (f[sel] * img.n_lines + l[sel]) * img.n_pixel + p[sel]
        m0 = np.bincount(flat, minlength=np.prod(shape)).astype(float)
        m1 = np.bincount(flat, weights=micro[i[sel]], minlength=np.prod(shape))
        # pixels below the photon threshold carry a -1 sentinel (the header
        # docstring says "zeros"; the code, and this pin, say -1 and use `<`)
        with np.errstate(invalid="ignore", divide="ignore"):
            ref = np.where(m0 >= n_min, m1 / m0 * res, -1.0).reshape(shape)
        np.testing.assert_allclose(got, ref, rtol=1e-12, atol=0)
        # stacked: photon-weighted mean over frames
        got_s = np.asarray(img.get_mean_micro_time(
            tttr, minimum_number_of_photons=n_min, stack_frames=True))
        m0s = m0.reshape(shape).sum(0)
        m1s = m1.reshape(shape).sum(0)
        with np.errstate(invalid="ignore", divide="ignore"):
            ref_s = np.where(m0s >= n_min, m1s / m0s * res, -1.0)
        np.testing.assert_allclose(got_s[0], ref_s, rtol=1e-12, atol=0)

    def test_moment_lifetime_image_is_m1_over_m0_minus_the_irf_moment(self):
        img, tttr = self.img, self.tttr
        micro = np.asarray(tttr.micro_times, dtype=np.float64)
        dt_ns = tttr.header.micro_time_resolution * 1e9
        n_min = 3
        m0_irf, m1_irf = 100.0, 250.0
        got = np.asarray(img.get_mean_lifetime(
            tttr, minimum_number_of_photons=n_min,
            m0_irf=m0_irf, m1_irf=m1_irf, stack_frames=True))
        f, l, p, i = self.ref
        sel = f < img.n_frames
        flat = l[sel] * img.n_pixel + p[sel]
        n = img.n_lines * img.n_pixel
        m0 = np.bincount(flat, minlength=n).astype(float)
        m1 = np.bincount(flat, weights=micro[i[sel]], minlength=n)
        with np.errstate(invalid="ignore", divide="ignore"):
            ref = np.where(m0 > n_min, (m1 / m0 - m1_irf / m0_irf) * dt_ns, 0.0)
        np.testing.assert_allclose(got[0].ravel(), ref, rtol=1e-10, atol=1e-12)

    def test_crop_and_rebin_are_slices_and_block_sums_of_the_reconstruction(self):
        img, tttr = self.img, self.tttr
        I = np.asarray(img.intensity, dtype=np.int64)
        c = tttrlib.CLSMImage(tttr_data=tttr, **HT3_PARAMS)
        c.fill(tttr_data=tttr, channels=[0])
        c.crop(2, 10, 16, 80, 32, 96)
        np.testing.assert_array_equal(np.asarray(c.intensity, dtype=np.int64),
                                      I[2:10, 16:80, 32:96])
        r = tttrlib.CLSMImage(tttr_data=tttr, **HT3_PARAMS)
        r.fill(tttr_data=tttr, channels=[0])
        r.rebin(4, 8)
        R = np.asarray(r.intensity, dtype=np.int64)
        # rebin keeps the pixel grid: block (bl, bp) is moved into pixel
        # (l // bl, p // bp), so the block sums land in the top-left corner and
        # every other pixel of the image is emptied
        block = I.reshape(I.shape[0], I.shape[1] // 4, 4, I.shape[2] // 8, 8).sum(axis=(2, 4))
        ref = np.zeros_like(I)
        ref[:, :block.shape[1], :block.shape[2]] = block
        np.testing.assert_array_equal(R, ref)


@unittest.skipIf(not DATA_AVAILABLE or not os.path.exists(SP5),
                 "CLSM SP5 sample not available")
class TestSp5RoutineAgainstNumpy(unittest.TestCase):
    """The SP5 routine (CLSMImage.cpp, `CLSM_SP5`): a frame starts at any event
    on routing channel 4 or 6; inside a frame the line edges are the frame
    start followed by every marker-type event on channels 1 or 2, in stream
    order, and consecutive edge pairs are the lines -- so the first line of a
    frame runs from the frame marker to the first line marker after it."""

    def test_intensity_image_is_the_marker_reconstruction(self):
        tttr = tttrlib.TTTR(SP5, "PTU")
        img = tttrlib.CLSMImage(tttr_data=tttr, reading_routine="SP5")
        img.fill(tttr_data=tttr, channels=[0])
        I = np.asarray(img.intensity, dtype=np.int64)
        mt = np.asarray(tttr.macro_times, dtype=np.int64)
        ch = np.asarray(tttr.routing_channels)
        et = np.asarray(tttr.event_types)
        fs = np.where(np.isin(ch, [4, 6]))[0]
        lm = np.where((et == 1) & np.isin(ch, [1, 2]))[0]
        ph = np.where((et == 0) & (ch == 0))[0]
        n_pix = img.n_pixel
        self.assertGreaterEqual(len(fs) - 1, img.n_frames)
        ref = np.zeros_like(I)
        for f in range(img.n_frames):
            a, b = fs[f], fs[f + 1]
            edges = [a] + list(lm[(lm > a) & (lm < b)])
            for l in range(min(img.n_lines, len(edges) // 2)):
                s, e = edges[2 * l], edges[2 * l + 1]
                t0, t1 = mt[s], mt[e]
                dwell = (t1 - t0) // n_pix
                if dwell <= 0:
                    continue
                i0, i1 = np.searchsorted(ph, s), np.searchsorted(ph, e)
                pix = (mt[ph[i0:i1]] - t0) // dwell
                np.add.at(ref[f, l], pix[pix < n_pix], 1)
        self.assertEqual(int(np.abs(I - ref).sum()), 0)
        self.assertGreater(int(I.sum()), 1_000_000)


if __name__ == "__main__":
    unittest.main()
