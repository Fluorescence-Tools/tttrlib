// SPDX-License-Identifier: BSD-3-Clause
//
// The marshalling contract of ext/js/jsarrays.i.
//
// Python's binding gets these guarantees from numpy.i and never tests them
// directly, because numpy.i is upstream and well covered. jsarrays.i is not: it
// is new code, it is the piece every other test depends on, and its failure
// modes are silent (a wrong element type read as garbage, a sliced view read
// from the wrong offset, a leaked or double-freed buffer). So it gets its own
// file, ahead of the analysis tests.

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { tttrlib, settings, dataPath, hasData } from './settings.mjs';

const REL = settings.spc132_filename;
const available = hasData(REL);

// Opened once, lazily. `before()` hooks are avoided throughout this suite:
// node:test ran some subtests ahead of the hook, leaving the fixture undefined
// in a scattered subset of them. A memoised factory has no ordering question.
let _spc = null;
const spc = () => (_spc ??= new tttrlib.TTTR(dataPath(REL), 'SPC-130'));


describe('TypedArray marshalling', { skip: !available && 'test data not downloaded' }, () => {

  test('64-bit data uses BigInt arrays, never Float64Array', () => {
    // A Float64Array is exact only below 2^53. Macro times here already exceed
    // that in aggregate, so the element type is a correctness property, not a
    // stylistic one.
    assert.ok(spc().get_macro_times() instanceof BigUint64Array);
  });

  test('narrow integer types keep their width', () => {
    assert.ok(spc().get_micro_times() instanceof Uint16Array);
    assert.ok(spc().get_routing_channel() instanceof Int8Array);
    assert.ok(spc().get_event_type() instanceof Int8Array);
  });

  test('a TypedArray of the wrong type is a TypeError, not a silent cast', () => {
    // get_tttr_by_channel takes signed char*; handing it a Float64Array must
    // fail loudly rather than reinterpret 8 bytes per channel.
    assert.throws(() => spc().get_tttr_by_channel(Float64Array.of(0, 8)),
                  /TypeError|wrong element type|Int8Array/);
  });

  test('a plain Array is accepted and copied', () => {
    assert.equal(spc().get_tttr_by_channel([0]).size(),
                 spc().get_tttr_by_channel(Int8Array.of(0)).size());
  });

  test('a subarray view is read from its byte offset, not from the buffer start', () => {
    // The classic ByteOffset bug: `arr.subarray(k)` shares the ArrayBuffer, so a
    // wrapper that ignores ByteOffset silently reads the first elements instead
    // of the intended ones.
    const chans = Int8Array.of(99, 99, 0);      // first two are nonsense
    const view = chans.subarray(2);              // ...and are skipped
    assert.equal(view.byteOffset, 2);
    assert.equal(spc().get_tttr_by_channel(view).size(),
                 spc().get_tttr_by_channel(Int8Array.of(0)).size());
  });

  test('multi-dimensional output carries its shape', () => {
    const t = new tttrlib.TTTR(dataPath(REL), 'SPC-130');
    const [hist] = t.get_microtime_histogram(1);
    // 1-D output stays a plain TypedArray: length says everything.
    assert.equal(hist.shape, undefined);
    assert.equal(hist.length, 4096);
  });

  test('array outputs report their copy mode', () => {
    assert.equal(typeof tttrlib.arraysAreZeroCopy(), 'boolean');
  });

  test('an empty selection yields an empty array, not a crash', () => {
    const empty = spc().get_tttr_by_channel(Int8Array.of(127)); // no such channel
    assert.equal(empty.size(), 0);
    assert.equal(empty.get_macro_times().length, 0);
  });
});

describe('numeric round-trips through std::vector', { skip: !available && 'test data not downloaded' }, () => {
  test('a vector<double> return is a Float64Array', () => {
    const cc = new tttrlib.CorrelatorCurve();
    cc.n_bins = 3;
    cc.n_casc = 5;
    const x = cc.get_x_axis();
    assert.ok(x instanceof Float64Array, `got ${x.constructor.name}`);
    assert.equal(x.length, cc.size());
  });
});
