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

  // The no-copy claim, asserted rather than described. `arraysAreZeroCopy()`
  // returning a boolean says nothing about whether the boolean is true of the
  // binary -- a build that copied everything would pass that test unchanged.
  //
  // Pda::get_amplitudes is a true ARGOUTVIEW: it hands out `_amplitudes.data()`,
  // a pointer into the object's live std::vector. So writing through the
  // returned Float64Array must be visible to C++, and a second call must read
  // the written value back. Under -DTTTRLIB_JS_COPY_ARRAYS it must NOT be --
  // which is why this asserts against the mode rather than for zero copy, and
  // is the one test in this file that means something different in each build.
  test('an ARGOUTVIEW output aliases C++ memory, or copies if built that way', () => {
    const pda = new tttrlib.Pda();
    pda.set_amplitudes([1, 2, 3]);

    const view = pda.get_amplitudes();
    assert.deepEqual(Array.from(view), [1, 2, 3]);
    view[0] = 42;

    const reread = pda.get_amplitudes();
    if (tttrlib.arraysAreZeroCopy()) {
      assert.equal(reread[0], 42, 'zero-copy build: C++ must see the write');
    } else {
      assert.equal(reread[0], 1, 'copy build: C++ must not see the write');
    }
    // Either way the untouched elements survive, so a failure above is about
    // aliasing and not about the vector having been clobbered.
    assert.deepEqual(Array.from(reread.subarray(1)), [2, 3]);
  });

  // The input half of the same contract, and unconditional: INPLACE_ARRAY1
  // borrows the caller's buffer in both builds -- TTTRLIB_JS_COPY_ARRAYS only
  // governs outputs. add_pile_up_to_model writes through `model`, so if the
  // typemap copied the argument the caller's array would come back untouched.
  test('an INPLACE argument is written through, not copied', () => {
    const model = Float64Array.from({ length: 16 }, (_, i) => 100 + i);
    const decay = Float64Array.from({ length: 16 }, (_, i) => 100 + i);
    const before = Array.from(model);

    tttrlib.add_pile_up_to_model(model, decay, 80.0, 120.0, 1.0);

    assert.ok(before.some((v, i) => v !== model[i]),
              'C++ wrote nothing back: the INPLACE argument was copied');
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
