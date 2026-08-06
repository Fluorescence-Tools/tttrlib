// SPDX-License-Identifier: BSD-3-Clause
//
// Canonical cross-language reference values.
//
// The SAME assertions are checked in the Python suite
// (test/python/tttr/test_cross_language_reference.py), the R suite
// (test/r/test_tttr.R) and the Java suite, so every language binding is verified
// to read identical data from the same file. The constants below are copied from
// the Python file and must not be changed on their own: if one of these fails
// here, the JavaScript binding is wrong, not the number.
//
// Reference file: bh/bh_spc132.spc read as container type "SPC-130".

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { tttrlib, settings, dataPath, hasData } from './settings.mjs';

// Canonical values (source of truth for all language bindings).
const REF_SIZE = 183657;
const REF_N_MICRO_CHANNELS = 4096;
const REF_MACRO_FIRST = 56916n;
const REF_SUM_MICRO = 242477881n;
const REF_SUM_MACRO = 443406877425185n;
const REF_SUM_ROUTING = 880650;
const REF_CORRCURVE_SIZE = 16; // CorrelatorCurve(n_bins=3, n_casc=5)
// burst_search(L=30, m=10, T=1e-3, mode="sliding_window") -> flat [start, stop, ...]
const REF_BURST_LEN = 586;
const REF_BURST_SUM = 59237329n;
// micro-time histogram (coarsening = 1)
const REF_HIST_LEN = 4096;
const REF_HIST_PEAK_CHAN = 814;
const REF_HIST_PEAK_VAL = 676;
// index getters (event 0) and header
const REF_MICRO_AT_0 = 1440;
const REF_ROUTING_AT_0 = 9;
const REF_MICRO_RES = 3.2958984375e-12;
// sub-selections
const REF_BY_CHANNEL_0 = 56499;
const REF_BY_CHANNEL_8 = 79468;
const REF_USED_CHANNELS = [0, 1, 8, 9];

const REL = settings.spc132_filename;
const available = hasData(REL);

// Opened once, lazily. `before()` hooks are avoided throughout this suite:
// node:test ran some subtests ahead of the hook, leaving the fixture undefined
// in a scattered subset of them. A memoised factory has no ordering question.
let _data = null;
const data = () => (_data ??= new tttrlib.TTTR(dataPath(REL), 'SPC-130'));

describe('cross-language reference (bh_spc132.spc)', { skip: !available && 'test data not downloaded' }, () => {
  test('size', () => {
    assert.equal(data().size(), REF_SIZE);
    assert.equal(data().get_n_valid_events(), REF_SIZE);
  });

  test('micro time channels', () => {
    assert.equal(data().get_number_of_micro_time_channels(), REF_N_MICRO_CHANNELS);
  });

  test('macro times', () => {
    const mt = data().macroTimes;
    // 64-bit macro times cross as a BigInt64Array. Summing a Float64Array here
    // would silently lose the low bits of a 4.4e14 total, which is exactly the
    // failure this binding is built to make impossible.
    assert.ok(mt instanceof BigUint64Array, `expected BigUint64Array, got ${mt.constructor.name}`);
    assert.equal(mt.length, REF_SIZE);
    assert.equal(mt[0], REF_MACRO_FIRST);
    assert.equal(mt.reduce((a, b) => a + b, 0n), REF_SUM_MACRO);
  });

  test('micro times', () => {
    const mt = data().microTimes;
    assert.equal(mt.reduce((a, b) => a + BigInt(b), 0n), REF_SUM_MICRO);
  });

  test('routing channels', () => {
    const rc = data().routingChannels;
    assert.equal(rc.reduce((a, b) => a + b, 0), REF_SUM_ROUTING);
  });

  test('correlator curve', () => {
    const cc = new tttrlib.CorrelatorCurve();
    cc.n_bins = 3;
    cc.n_casc = 5;
    assert.equal(cc.size(), REF_CORRCURVE_SIZE);
  });

  test('burst search', () => {
    const r = data().burst_search(30, 10, 1e-3, 'sliding_window');
    assert.equal(r.length, REF_BURST_LEN);
    assert.equal(Array.from(r).reduce((a, b) => a + BigInt(b), 0n), REF_BURST_SUM);
  });

  test('microtime histogram', () => {
    const [hist] = data().get_microtime_histogram(1);
    assert.equal(hist.length, REF_HIST_LEN);
    let peakChan = 0;
    for (let i = 1; i < hist.length; i++) if (hist[i] > hist[peakChan]) peakChan = i;
    assert.equal(peakChan, REF_HIST_PEAK_CHAN);
    assert.equal(hist[peakChan], REF_HIST_PEAK_VAL);
  });

  test('index getters', () => {
    assert.equal(BigInt(data().get_macro_time_at(0)), REF_MACRO_FIRST);
    assert.equal(Number(data().get_micro_time_at(0)), REF_MICRO_AT_0);
    assert.equal(Number(data().get_routing_channel_at(0)), REF_ROUTING_AT_0);
  });

  test('micro time resolution', () => {
    assert.equal(data().get_header().micro_time_resolution, REF_MICRO_RES);
  });

  test('tttr by channel', () => {
    assert.equal(data().get_tttr_by_channel(Int8Array.of(0)).size(), REF_BY_CHANNEL_0);
    assert.equal(data().get_tttr_by_channel(Int8Array.of(8)).size(), REF_BY_CHANNEL_8);
  });

  test('used routing channels', () => {
    const used = Array.from(data().get_used_routing_channels()).map(Number).sort((a, b) => a - b);
    assert.deepEqual(used, REF_USED_CHANNELS);
  });

  test('header json', () => {
    assert.ok(data().get_header().get_json().includes('MeasDesc_ContainerType'));
  });
});
