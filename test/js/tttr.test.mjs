// SPDX-License-Identifier: BSD-3-Clause
//
// Core TTTR reading, selection and header handling -- the JavaScript counterpart
// of test/python/tttr/.

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { tttrlib, settings, dataPath, hasData } from './settings.mjs';

const SPC = settings.spc132_filename;
const PTU_T3 = 'pq/ptu/pq_ptu_hh_t3.ptu';
const PTU_T2 = 'pq/ptu/pq_ptu_hh_t2.ptu';
const HT3 = 'pq/ht3/pq_ht3v1.0_hh_t3.ht3';

// Opened once, lazily. `before()` hooks are avoided throughout this suite:
// node:test ran some subtests ahead of the hook, leaving the fixture undefined
// in a scattered subset of them. A memoised factory has no ordering question.
let _spc = null;
const spc = () => (_spc ??= new tttrlib.TTTR(dataPath(SPC), 'SPC-130'));


describe('TTTR: readers', () => {
  test('PTU T3 opens with the container inferred from content', { skip: !hasData(PTU_T3) && 'no data' }, () => {
    const t = new tttrlib.TTTR(dataPath(PTU_T3));
    assert.ok(t.size() > 0);
    assert.ok(t.get_macro_times() instanceof BigUint64Array);
    assert.equal(t.get_macro_times().length, t.size());
    assert.equal(t.get_micro_times().length, t.size());
    assert.equal(t.get_routing_channel().length, t.size());
  });

  test('PTU T2 opens', { skip: !hasData(PTU_T2) && 'no data' }, () => {
    const t = new tttrlib.TTTR(dataPath(PTU_T2));
    assert.ok(t.size() > 0);
  });

  test('HT3 opens', { skip: !hasData(HT3) && 'no data' }, () => {
    const t = new tttrlib.TTTR(dataPath(HT3));
    assert.ok(t.size() > 0);
  });

  test('SPC opens with an explicit container type', { skip: !hasData(SPC) && 'no data' }, () => {
    const t = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
    assert.ok(t.size() > 0);
  });

  test('a missing file yields an empty TTTR, as it does in Python', () => {
    // Not a throw. tttrlib warns and returns an empty object -- surprising, but
    // it is what `tttrlib.TTTR('/nonexistent', 'PTU')` does in Python too, and a
    // binding that raised here would disagree with the reference one.
    assert.equal(new tttrlib.TTTR('/nonexistent/file.ptu', 'PTU').size(), 0);
  });
});

describe('TTTR: header', { skip: !hasData(PTU_T3) && 'no data' }, () => {
  let _hdr = null;
  const header = () => (_hdr ??= new tttrlib.TTTR(dataPath(PTU_T3)).get_header());

  test('resolutions are positive and in seconds', () => {
    assert.ok(header().macro_time_resolution > 0);
    assert.ok(header().micro_time_resolution > 0);
    // A macro time step is longer than a micro time step in every TTTR format.
    assert.ok(header().macro_time_resolution > header().micro_time_resolution);
  });

  test('the JSON payload parses and names the container', () => {
    const j = JSON.parse(header().get_json());
    assert.equal(typeof j, 'object');
    assert.ok(header().get_json().includes('MeasDesc_ContainerType'));
  });

  test('the `json` property mirrors get_json()', () => {
    assert.equal(header().json, header().get_json());
  });
});

describe('TTTR: selections', { skip: !hasData(SPC) && 'no data' }, () => {

  test('channel selection partitions the events', () => {
    const used = Array.from(spc().get_used_routing_channels()).map(Number);
    const total = used.reduce((n, c) => n + spc().get_tttr_by_channel(Int8Array.of(c)).size(), 0);
    assert.equal(total, spc().size());
  });

  test('an index selection produces a TTTR of that length', () => {
    const sel = Int32Array.from({ length: 100 }, (_, i) => i * 7);
    const sub = new tttrlib.TTTR(spc(), sel);
    assert.equal(sub.size(), 100);
    assert.equal(sub.get_macro_times()[0], spc().get_macro_times()[0]);
  });

  test('select() takes indices, arrays and ranges', () => {
    assert.equal(spc().select(5).size(), 1);
    assert.equal(spc().select([1, 2, 3]).size(), 3);
    assert.equal(spc().select({ start: 0, stop: 10 }).size(), 10);
  });

  test('count-rate selection returns a subset', () => {
    const sub = spc().get_tttr_by_count_rate(1e-3, 30, false, false);
    assert.ok(sub.size() <= spc().size());
  });

  test('concat appends events', () => {
    const doubled = spc().concat(spc());
    assert.equal(doubled.size(), 2 * spc().size());
  });

  test('macro times are non-decreasing', () => {
    const mt = spc().get_macro_times();
    for (let i = 1; i < Math.min(mt.length, 20000); i++) {
      if (mt[i] < mt[i - 1]) assert.fail(`macro time went backwards at ${i}`);
    }
  });
});

describe('TTTR: micro-time histogram', { skip: !hasData(SPC) && 'no data' }, () => {
  test('coarsening divides the channel count and preserves the total', () => {
    const t = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
    const [fine] = t.get_microtime_histogram(1);
    const [coarse] = t.get_microtime_histogram(4);
    assert.equal(coarse.length, fine.length / 4);
    const sum = (a) => a.reduce((x, y) => x + y, 0);
    assert.equal(sum(coarse), sum(fine));
    assert.equal(sum(fine), t.size());
  });
});
