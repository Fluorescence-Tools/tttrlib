// SPDX-License-Identifier: BSD-3-Clause
//
// The analysis surface: correlation, imaging, burst filtering, decay, phasors,
// PDA and the simulator.
//
// Counterpart of test/python/{correlator,clsm,burstfilter,decayfit,pda,simulation}/.
// The assertions here are the invariants those suites check -- shapes,
// conservation laws, monotonicity, and agreement between two routes to the same
// number -- rather than a second copy of Python's magic constants. The canonical
// values that MUST agree across bindings live in
// test_cross_language_reference.mjs, and belong there and nowhere else.

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { tttrlib, settings, dataPath, hasData } from './settings.mjs';

const SPC = settings.spc132_filename;
const PTU_T3 = 'pq/ptu/pq_ptu_hh_t3.ptu';
const HT3_CLSM = 'imaging/pq/ht3/pq_ht3_clsm.ht3';

// Opened once, lazily -- see the note in cross_language_reference.test.mjs on
// why this suite avoids before() hooks.
let _spc = null;
const spc = () => (_spc ??= new tttrlib.TTTR(dataPath(SPC), 'SPC-130'));


// ---------------------------------------------------------------------------
describe('Correlator', { skip: !hasData(SPC) && 'no data' }, () => {

  test('a full correlation produces a monotone, positive lag axis', () => {
    const ch0 = spc().get_tttr_by_channel(Int8Array.of(0));
    const ch8 = spc().get_tttr_by_channel(Int8Array.of(8));
    const c = new tttrlib.Correlator();
    c.n_bins = 8;
    c.n_casc = 12;
    c.set_tttr(ch0, ch8);
    const x = c.get_x_axis();
    const y = c.get_corr_normalized();
    assert.ok(x instanceof Float64Array);
    assert.equal(x.length, y.length);
    assert.ok(x.length > 0);
    for (let i = 1; i < x.length; i++) {
      assert.ok(x[i] > x[i - 1], `lag axis not increasing at ${i}`);
    }
    assert.ok(y.every(Number.isFinite), 'correlation contains a non-finite value');
  });

  test('the curve length follows n_bins and n_casc', () => {
    const cc = new tttrlib.CorrelatorCurve();
    cc.n_bins = 8;
    cc.n_casc = 10;
    const bigger = new tttrlib.CorrelatorCurve();
    bigger.n_bins = 8;
    bigger.n_casc = 20;
    assert.ok(bigger.size() > cc.size());
  });
});

// ---------------------------------------------------------------------------
describe('CLSMImage', { skip: !hasData(HT3_CLSM) && 'no data' }, () => {
  test('an intensity image has three dimensions and conserves photons', () => {
    const t = new tttrlib.TTTR(dataPath(HT3_CLSM));
    // CLSMImage's later parameters have C++ defaults, but SWIG's Node-API
    // dispatch matches on argument count, so pass the channel list explicitly.
    const img = new tttrlib.CLSMImage(t, new tttrlib.CLSMSettings(), null, true,
                                      Int32Array.of(0));
    const intensity = img.get_intensity();

    // Multi-dimensional results are flat and row-major with the shape attached,
    // which is the one convention a JavaScript caller has to learn.
    assert.ok(Array.isArray(intensity.shape), 'intensity carries no shape');
    assert.equal(intensity.shape.length, 3);
    const [frames, lines, pixels] = intensity.shape;
    assert.equal(intensity.length, frames * lines * pixels);
    assert.equal(frames, img.n_frames);
    assert.equal(lines, img.n_lines);
    assert.equal(pixels, img.n_pixel);

    // Every photon in the image is a photon in the file.
    const total = intensity.reduce((a, b) => a + b, 0);
    assert.ok(total > 0, 'image is empty');
    assert.ok(total <= t.size(), 'image holds more photons than the file');
  });
});

// ---------------------------------------------------------------------------
describe('burst search and filtering', { skip: !hasData(SPC) && 'no data' }, () => {

  test('burst boundaries are in range and strictly advancing', () => {
    const r = spc().burst_search(30, 10, 1e-3, 'sliding_window');
    const n = BigInt(spc().size());
    let prevStart = -1n;
    let prevStop = -1n;
    for (let i = 0; i < r.length; i += 2) {
      assert.ok(r[i] >= 0n && r[i + 1] < n, `burst ${i / 2} out of range`);
      assert.ok(r[i] <= r[i + 1], `burst ${i / 2} ends before it starts`);
      // Successive bursts advance, but they are NOT disjoint: a sliding-window
      // search can end one burst and start the next on overlapping photons
      // (the reference file's first two are [237, 371] and [370, 491]). That is
      // the C++ behaviour every binding sees, so the invariant asserted here is
      // advancement, not disjointness.
      assert.ok(r[i] > prevStart, `burst ${i / 2} does not start after the previous one`);
      assert.ok(r[i + 1] > prevStop, `burst ${i / 2} does not end after the previous one`);
      prevStart = r[i];
      prevStop = r[i + 1];
    }
  });

  test('a stricter minimum burst size never yields more bursts', () => {
    const loose = spc().burst_search(20, 10, 1e-3, 'sliding_window').length;
    const strict = spc().burst_search(200, 10, 1e-3, 'sliding_window').length;
    assert.ok(strict <= loose);
  });

  test('BurstFilter round-trips its settings through JSON', () => {
    // BurstFilter has no default constructor: it is built around a photon
    // stream. Passing the TTTR also exercises the shared_ptr<TTTR> argument
    // path, which is the whole reason ext/js/js_shared_ptr.i exists.
    const bf = new tttrlib.BurstFilter(spc());
    const json = bf.to_json_string();
    assert.equal(typeof JSON.parse(json), 'object');
    const other = new tttrlib.BurstFilter(spc());
    other.from_json_string(json);
    assert.equal(other.to_json_string(), json);
  });
});

// ---------------------------------------------------------------------------
describe('decay analysis', { skip: !hasData(SPC) && 'no data' }, () => {
  test('a phasor of a real decay lands inside the universal semicircle', () => {
    const [hist] = spc().get_microtime_histogram(1);
    // compute_phasor_bincounts takes std::vector<int>, so the counts cross as an
    // Int32Array. A Float64Array is rejected rather than silently reinterpreted
    // -- which is the point of the strict element-type rule in jsarrays.i.
    const counts = Int32Array.from(hist);
    // One full period across the micro-time window.
    const freq = 1.0 / (hist.length * spc().get_header().micro_time_resolution);
    const [g, s] = tttrlib.DecayPhasor.compute_phasor_bincounts(counts, freq, 1, 1.0, 0.0);
    assert.ok(Number.isFinite(g) && Number.isFinite(s));
    // A physical decay sits on or inside the semicircle through (0,0) and
    // (1,0), whose apex is (0.5, 0.5).
    assert.ok(g >= -0.05 && g <= 1.05, `g out of range: ${g}`);
    assert.ok(s >= -0.05 && s <= 0.55, `s out of range: ${s}`);
  });

  test('a single-exponential decay lands ON the semicircle', () => {
    // g^2 + s^2 = g is the defining relation of the universal semicircle, and a
    // pure single exponential satisfies it exactly. A known answer, so this
    // catches a phasor that is merely plausible.
    const n = 4096;
    const tau = 200.0;                       // in bins
    const counts = Int32Array.from({ length: n }, (_, i) => Math.round(1e6 * Math.exp(-i / tau)));
    const [g, s] = tttrlib.DecayPhasor.compute_phasor_bincounts(counts, 1.0 / n, 1, 1.0, 0.0);
    assert.ok(Math.abs(g * g + s * s - g) < 1e-3,
              `off the semicircle: g=${g}, s=${s}, residual=${g * g + s * s - g}`);
  });
});

// ---------------------------------------------------------------------------
describe('simulator', { skip: !tttrlib.SimEngine && 'built without the photon simulator' }, () => {
  /**
   * One immobile emitter at the focal centre, two equally bright channels --
   * a transcription of `_point_sample` in test/python/simulation/test_engine.py.
   * add_fluorophore() is what actually puts a molecule in the box; without it
   * the engine runs and emits nothing.
   */
  function engine(nPhotons) {
    const sample = new tttrlib.SimSystem();
    const species = new tttrlib.SimSpecies();
    species.D = 0.0;
    species.q = Float64Array.of(50.0, 50.0);
    sample.add_species(species);
    sample.set_rate_matrices(Float64Array.of(0.0), Float64Array.of(0.0));
    sample.set_background(Float64Array.of(0.0, 0.0));
    sample.add_fluorophore(0.0, 0.0, 0.0, 0, false);

    const excitation = tttrlib.SimGrid.gaussian3d(0.3, 1.0, 1.5, 3.0, 0.05, 1.0);
    const integrator = new tttrlib.SimIntegrator();
    integrator.dt = 0.01;
    integrator.n_channels = 2;
    integrator.n_ph_max = nPhotons;
    integrator.max_windows = 10 ** 8;

    return new tttrlib.SimEngine(sample, excitation, new tttrlib.VectorSimGrid(), integrator);
  }

  test('a run produces the requested photons on both channels', () => {
    const eng = engine(20000);
    eng.run();
    assert.ok(eng.n_photons() >= 20000, `only ${eng.n_photons()} photons`);
    const ch = eng.channel();
    assert.equal(ch.length, eng.n_photons());
    // Two channels of equal brightness: neither may be starved.
    const frac0 = Array.from(ch).filter((c) => c === 0).length / ch.length;
    assert.ok(Math.abs(frac0 - 0.5) < 0.05, `channel 0 got ${frac0}`);
  });

  test('photons within a macro window are time-ordered', () => {
    const eng = engine(20000);
    eng.run();
    const W = eng.macro_window();
    const t = eng.arrival_time();
    for (let i = 1; i < W.length; i++) {
      if (W[i] === W[i - 1]) {
        assert.ok(t[i] >= t[i - 1], `arrival times out of order at ${i}`);
      }
    }
  });

  test('a config object drives a run, and the seed selects the stream', () => {
    // The JavaScript form of Python's SimEngine.from_dict: one JSON entry
    // point, so the SAME config object runs unchanged in either binding. This
    // one is the Python suite's, transcribed.
    const config = {
      settings: { dt: 0.01, n_ph_max: 40000, max_windows: 10 ** 8,
                  seed_diffusion: 111, seed_emission: 222, n_channels: 2,
                  rng_kind: 'pcg', rng_scope: 'per_molecule' },
      box: { xy: 2.0, z: 4.0 },
      species: [{ D: 3.0, q: [50.0, 50.0] }],
      k_rad: [0.0], k_nrad: [0.0], background: [0.0, 0.0], population: [5.0],
      excitation: { type: 'gaussian3d', w0: 0.3, z0: 2.0,
                    extent_xy: 2.0, extent_z: 4.0, spacing: 0.1, amplitude: 1.0 },
      detection: [],
    };
    const e1 = tttrlib.SimEngine.fromObject(config);
    e1.run();
    assert.ok(e1.n_photons() > 0);

    config.settings.seed_diffusion = 999;
    const e2 = tttrlib.SimEngine.fromObject(config);
    e2.run();
    assert.ok(e2.n_photons() > 0);

    const a = e1.macro_window();
    const b = e2.macro_window();
    assert.ok(a.length !== b.length ||
              !a.every((v, i) => v === b[i]), 'a different seed gave the same stream');
  });

  test('default_json() parses and builds an engine', () => {
    const cfg = tttrlib.SimEngine.default_json();
    assert.equal(typeof JSON.parse(cfg), 'object');
    assert.equal(tttrlib.SimEngine.from_json(cfg).n_photons(), 0);
  });
});

// ---------------------------------------------------------------------------
describe('histograms', () => {
  test('a 1-D histogram counts every sample that falls in range', () => {
    // Values in [0, 1), ten evenly-populated bins.
    const values = Float64Array.from({ length: 1000 }, (_, i) => (i % 100) / 100);
    const h = new tttrlib.doubleHistogram();
    h.set_axis(0, 'x', 0.0, 1.0, 10, 'lin');
    h.update({ data: values, shape: [values.length, 1] });
    const counts = h.get_histogram();
    // Shape and conservation are the binding's business; exactly where a value
    // on a bin edge lands is the C++ axis's, and is asserted in the Python
    // suite where that behaviour belongs.
    assert.equal(counts.length, 10);
    assert.equal(counts.reduce((a, b) => a + b, 0), values.length);
    assert.ok(counts.every((c) => c > 0), 'some bin got nothing from a uniform sample');
  });
});
