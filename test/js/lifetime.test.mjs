// SPDX-License-Identifier: BSD-3-Clause
//
// Object lifetime across the binding boundary.
//
// PRD-016 names this as the risk that bites hardest, because its failure mode is
// a segfault rather than an exception: a TypedArray that outlives the C++ object
// owning its memory, or a shared_ptr whose last reference is dropped while
// JavaScript still holds a proxy. Neither shows up in a test that only checks
// values, so the checks live here.
//
// Run with --expose-gc to make the collection deterministic:
//
//     node --expose-gc --test test/js/lifetime.test.mjs
//
// Without it the GC-forcing tests skip rather than pass vacuously.

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { tttrlib, settings, dataPath, hasData } from './settings.mjs';

const SPC = settings.spc132_filename;
const gc = globalThis.gc;
const noGc = 'run with --expose-gc';

describe('lifetime', { skip: !hasData(SPC) && 'no data' }, () => {
  test('a shared_ptr result outlives the call that produced it', () => {
    // get_tttr_by_channel returns a shared_ptr whose only reference IS the
    // return value. If the binding handed out the raw pointer without retaining
    // that reference, the object would be destroyed before this line runs and
    // the read below would be a use-after-free.
    let sub;
    {
      const parent = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
      sub = parent.get_tttr_by_channel(Int8Array.of(0));
    }
    assert.ok(sub.size() > 0);
    assert.equal(sub.get_macro_times().length, sub.size());
  });

  test('a shared_ptr result survives collection of the object it came from',
       { skip: !gc && noGc }, () => {
    let sub;
    {
      let parent = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
      sub = parent.get_tttr_by_channel(Int8Array.of(0));
      parent = null;
    }
    gc(); gc();
    // The holder table in ext/js/js_shared_ptr.i is what keeps this alive.
    assert.ok(sub.size() > 0);
    const mt = sub.get_macro_times();
    assert.ok(mt.length > 0);
    assert.ok(mt[0] >= 0n);
  });

  test('many proxies of the same object release cleanly',
       { skip: !gc && noGc }, () => {
    // The holder table refcounts per proxy: a leak would grow the table without
    // bound, and an over-release would free the object while another proxy still
    // points at it. Churn a few thousand and then use one.
    const parent = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
    let last;
    for (let i = 0; i < 2000; i++) last = parent.get_tttr_by_channel(Int8Array.of(0));
    gc(); gc();
    assert.ok(last.size() > 0);
    assert.ok(parent.size() > 0);
  });

  test('a copied view is safe after its owner is collected',
       { skip: !gc && noGc }, () => {
    // The documented escape hatch: .slice() detaches from C++-owned memory, so
    // the copy stays valid however the owner is disposed of.
    let copy;
    let expected;
    {
      let t = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
      const view = t.get_macro_times();
      expected = view[0];
      copy = view.slice(0, 1000);
      t = null;
    }
    gc(); gc();
    assert.equal(copy.length, 1000);
    assert.equal(copy[0], expected);
  });

  test('reading a file repeatedly does not grow the heap without bound',
       { skip: !gc && noGc }, () => {
    // A crude leak check: the ARGOUTVIEWM finalizers must actually free. If they
    // did not, 200 reads of a 183k-photon file would show a monotone climb far
    // beyond the noise of ordinary allocation.
    const read = () => {
      const t = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
      return t.get_macro_times().length + t.get_micro_times().length;
    };
    for (let i = 0; i < 20; i++) read();      // warm up
    gc(); gc();
    const before = process.memoryUsage().external + process.memoryUsage().heapUsed;
    for (let i = 0; i < 200; i++) read();
    gc(); gc();
    const after = process.memoryUsage().external + process.memoryUsage().heapUsed;
    const grewMB = (after - before) / 1e6;
    // 200 leaked reads would be ~400 MB of photon arrays; anything under a few
    // tens of MB is ordinary allocator noise.
    assert.ok(grewMB < 64, `heap grew ${grewMB.toFixed(1)} MB over 200 reads`);
  });
});
