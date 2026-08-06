// SPDX-License-Identifier: BSD-3-Clause
//
// The subsystems that reach JavaScript through the shared interface files but
// were never exercised from it: the simulator, BVA, 2CDE, the HMMs, the neural
// net and CSV. All six are wrapped for JavaScript; what this file pins is that
// they are usable, which is a different claim and was not true.
//
// Two things it guards in particular:
//
//   * The analysis OUTPUTS. Python gets `photons()`, `.result`,
//     `.proximity_ratio_mean`, `layer_weights()` and friends from %pythoncode,
//     which no other backend reaches. ext/js/pkg/index.js now provides the same
//     shorthand over the same C++, so a simulated photon stream is one call
//     rather than seven parallel accessors a caller has to know are parallel.
//
//   * Exception translation. A C++ throw that reaches the Node-API boundary
//     untranslated does not become a JavaScript error -- it TERMINATES THE
//     PROCESS. Everything from BurstFeature.i to HmmSurrogate.i used to be in
//     that state, because BurstFeatureExtractor.i ended its %exception with a
//     bare `%exception;`, which clears the handler globally rather than
//     restoring it. Python survived on SWIG-Python's built-in fallback; Node
//     did not.

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';

import { fileURLToPath } from 'node:url';

import { tttrlib, settings, dataPath, hasData } from './settings.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));      // test/js
const REPO = path.dirname(path.dirname(HERE));                  // repository root
const SIM_CONFIG = path.join(REPO, 'examples', 'simulation', 'configs',
                             'single_molecule_fcs.json');

const SPC = settings.spc132_filename;
const haveSpc = hasData(SPC);
let _spc = null;
const spc = () => (_spc ??= new tttrlib.TTTR(dataPath(SPC), 'SPC-130'));

/** Burst bounds as the (n, 2) block BVA and 2CDE take. */
const burstBlock = () => {
  const flat = spc().burst_search(30, 10, 1e-3, 'sliding_window');
  return { data: flat, shape: [flat.length / 2, 2] };
};

// ---------------------------------------------------------------------------
describe('exception translation', { skip: !haveSpc && 'no data' }, () => {
  test('a C++ throw becomes a JS Error instead of killing the process', () => {
    // BVA::compute without streams throws std::runtime_error. Before the
    // handler was restored this aborted node outright, so the assertion below
    // could not even run -- the test process died.
    const bf = new tttrlib.BurstFilter(spc());
    bf.find_bursts();
    assert.throws(() => new tttrlib.BVA(bf).compute(), /unknown stream|BurstFeature/);
  });
});

// ---------------------------------------------------------------------------
describe('simulator', () => {
  test('a run produces a readable photon stream', () => {
    // No silent skip on a missing config: it is committed alongside this test,
    // so its absence is a broken checkout, not a reason to pass quietly.
    assert.ok(fs.existsSync(SIM_CONFIG), `simulator config missing: ${SIM_CONFIG}`);
    const cfg = JSON.parse(fs.readFileSync(SIM_CONFIG, 'utf8'));
    cfg.settings.n_ph_max = 2000;

    const engine = tttrlib.SimEngine.from_json(JSON.stringify(cfg));
    engine.run();

    const ph = engine.photons();
    const lengths = Object.values(ph).map((a) => a.length);
    assert.equal(new Set(lengths).size, 1,
                 'the seven channels are parallel arrays and must agree in length');
    assert.equal(ph.channel.length, Number(engine.n_photons()));
    assert.ok(ph.channel.length > 0, 'the simulation produced no photons');
  });
});

// ---------------------------------------------------------------------------
describe('burst features', { skip: !haveSpc && 'no data' }, () => {
  test('BVA yields one proximity ratio per burst', () => {
    const bursts = burstBlock();
    const bva = new tttrlib.BVA(spc());
    bva.set_donor(Int32Array.of(0));
    bva.set_acceptor(Int32Array.of(8));
    bva.compute(bursts, 4, 0.01);

    const mean = bva.proximityRatioMean;
    const std = bva.proximityRatioStd;
    assert.equal(mean.length, bursts.shape[0]);
    assert.equal(std.length, mean.length);
    // A proximity ratio is a fraction; NaN is allowed where a burst is too
    // small to define one, but a value outside [0, 1] never is.
    for (const v of mean) assert.ok(Number.isNaN(v) || (v >= 0 && v <= 1), `ratio ${v}`);
  });

  test('2CDE yields one value per burst', () => {
    const bursts = burstBlock();
    const cde = new tttrlib.TwoCDE(spc());
    cde.set_donor(Int32Array.of(0));
    cde.set_acceptor(Int32Array.of(8));
    cde.compute(bursts);
    assert.equal(cde.twoCde.length, bursts.shape[0]);
  });
});

// ---------------------------------------------------------------------------
describe('neural net', () => {
  // Two layers with known weights, so the forward pass is checkable by hand:
  //   [1, 2] -> [0.5, 1.1, 1.7] (relu, unchanged) -> 0.5+1.1+1.7 + 0.5 = 3.8
  const SPEC = JSON.stringify({
    layers: [
      { n_in: 2, n_out: 3, activation: 'relu', weight: [0.1, 0.2, 0.3, 0.4, 0.5, 0.6], bias: [0, 0, 0] },
      { n_in: 3, n_out: 1, activation: 'identity', weight: [1, 1, 1], bias: [0.5] },
    ],
  });

  test('a forward pass gives the arithmetic answer', () => {
    const net = tttrlib.NeuralNet.from_json_string(SPEC);
    assert.equal(Number(net.n_layers()), 2);
    assert.equal(Number(net.n_inputs()), 2);
    const y = net.predict(Float64Array.of(1.0, 2.0));
    assert.equal(y.length, 1);
    assert.ok(Math.abs(y[0] - 3.8) < 1e-9, `expected 3.8, got ${y[0]}`);
  });

  test('weights come back shaped, not flat', () => {
    const net = tttrlib.NeuralNet.from_json_string(SPEC);
    const w = net.layerWeights(0);
    assert.equal(w.length, 3, 'one row per output');
    assert.equal(w[0].length, 2, 'one column per input');
    assert.deepEqual(Array.from(net.layerBias(1)), [0.5]);
  });
});

// ---------------------------------------------------------------------------
describe('hidden Markov models', () => {
  test('the core surface is bound', () => {
    const hmm = new tttrlib.HMM();
    for (const m of ['fit', 'evaluate', 'viterbi', 'posterior', 'sample_states', 'sample_paths']) {
      assert.equal(typeof hmm[m], 'function', `HMM.${m} is not callable`);
    }
    // The conveniences ext/js/pkg/index.js adds over those.
    for (const m of ['viterbiPath', 'gamma', 'jitterPath', 'ffbsPaths']) {
      assert.equal(typeof hmm[m], 'function', `HMM.${m} is missing`);
    }
  });
});

// ---------------------------------------------------------------------------
describe('csv', () => {
  test('a DataStore round-trips through a CSV file', () => {
    const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'tttrlib-js-csv-'));
    const file = path.join(dir, 'store.csv');
    try {
      const out = new tttrlib.DataStore();
      const col = out.column(out.add_column('x', tttrlib.ColumnType_Float64));
      col.set_f64(Float64Array.of(1.5, 2.5, 3.5));
      out.set_n_rows(3);
      tttrlib.write_csv(file, out, new tttrlib.CsvWriteOptions());

      const back = new tttrlib.DataStore();
      tttrlib.read_csv_into(back, file, new tttrlib.CsvOptions());
      assert.equal(Number(back.n_rows()), 3);
      assert.equal(Number(back.n_columns()), 1);
      assert.deepEqual(Array.from(back.column_by_name('x').get_f64_view()), [1.5, 2.5, 3.5]);
    } finally {
      fs.rmSync(dir, { recursive: true, force: true });
    }
  });
});
