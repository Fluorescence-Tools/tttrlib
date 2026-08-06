// SPDX-License-Identifier: BSD-3-Clause
//
// The JavaScript runner of the cross-language conformance suite.
//
// A port of test/conformance/py/interpreter.py: the same case list, the same
// vocabulary, the same expected values. A failure here means the JavaScript
// binding disagrees with Python, R and Java -- never that the number needs
// updating. See test/conformance/README.md.
//
// The one thing this runner does that the others do not is carry 64-bit data as
// BigInt. jsarrays.i returns BigUint64Array for macro times precisely because a
// Float64Array would be exact only below 2^53, so the reduction has to stay in
// BigInt and convert once, at the end, when the value is known to fit.

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync, readdirSync, writeFileSync, mkdtempSync, rmSync, existsSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

import { tttrlib, DATA_ROOT, dataPath } from './settings.mjs';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const CASES_DIR = path.join(path.dirname(HERE), 'conformance', 'cases');

/** 2^53: past here a JavaScript number no longer holds every integer. */
const MAX_EXACT_INTEGER = 2 ** 53;

const COLUMN_TYPES = {
  f64: 'Float64', f32: 'Float32', i64: 'Int64', i32: 'Int32', i16: 'Int16',
  i8: 'Int8', u64: 'UInt64', u32: 'UInt32', u16: 'UInt16', u8: 'UInt8',
};
const DTYPE_NAMES = {
  Float64: 'float64', Float32: 'float32', Int64: 'int64', Int32: 'int32',
  Int16: 'int16', Int8: 'int8', UInt64: 'uint64', UInt32: 'uint32',
  UInt16: 'uint16', UInt8: 'uint8', Bool: 'bool', String: 'string',
};
const columnTypeValue = (name) => tttrlib[`ColumnType_${name}`];
const dtypeNameOf = (value) => {
  for (const [name, canonical] of Object.entries(DTYPE_NAMES))
    if (columnTypeValue(name) === value) return canonical;
  throw new Error(`dtype ${value} is not in the canonical set`);
};

class UnsupportedOp extends Error {
  constructor(op) { super(op); this.op = op; }
}

// ---------------------------------------------------------------------------
// Value helpers
// ---------------------------------------------------------------------------

const isTypedArray = (v) => ArrayBuffer.isView(v) && !(v instanceof DataView);
const isStringList = (v) => Array.isArray(v) && v.every((x) => typeof x === 'string');

/** Any element as a plain number, whatever width it arrived in. */
const num = (x) => (typeof x === 'bigint' ? Number(x) : Number(x));

function asSequence(on) {
  if (isTypedArray(on) || Array.isArray(on) || typeof on === 'string') return on;
  throw new Error(`expected a sequence, got ${typeof on}`);
}

/**
 * Sum an array without losing the exactness the element type promised.
 *
 * A BigInt64Array is summed in BigInt and converted once, so a total below 2^53
 * is exact even when the intermediate sum would not survive a double. Above it,
 * the conversion is refused rather than performed approximately -- the case is
 * wrong, and the runners agree on saying so.
 */
function sumOf(on) {
  const a = asSequence(on);
  if (a.length && typeof a[0] === 'bigint') {
    let t = 0n;
    for (const x of a) t += x;
    if (t > BigInt(Number.MAX_SAFE_INTEGER) || t < -BigInt(Number.MAX_SAFE_INTEGER))
      throw new Error(`sum ${t} is past 2^53 and cannot be compared exactly`);
    return Number(t);
  }
  let t = 0;
  for (const x of a) t += num(x);
  return t;
}

function extremeOf(on, better) {
  const a = asSequence(on);
  let best = num(a[0]);
  for (let k = 1; k < a.length; k++) {
    const v = num(a[k]);
    if (better(v, best)) best = v;
  }
  return best;
}

function argExtreme(on, better) {
  const a = asSequence(on);
  let best = 0;
  for (let k = 1; k < a.length; k++) if (better(num(a[k]), num(a[best]))) best = k;
  return best;
}

// ---------------------------------------------------------------------------
// The dispatch table -- one entry per op in test/conformance/OPS.md
// ---------------------------------------------------------------------------

function makeOps(ctx) {
  const store = (on) => on;

  const addTyped = (sfx) => (on, a) => {
    const col = on.column(on.add_column(a[0], columnTypeValue(COLUMN_TYPES[sfx])));
    const values = a[1];
    const setter = `set_${sfx}`;
    col[setter](makeTyped(sfx, values));
    if (Number(on.n_rows()) < values.length) on.set_n_rows(values.length);
  };

  const columnTyped = (sfx) => (on, a) => {
    const col = on.column_by_name(a[0]);
    const want = columnTypeValue(COLUMN_TYPES[sfx]);
    if (col.type() !== want)
      throw new Error(`column '${a[0]}' is ${dtypeNameOf(col.type())}, `
                    + `the case asked for ${DTYPE_NAMES[COLUMN_TYPES[sfx]]}`);
    return col[`get_${sfx}_view`]();
  };

  const ops = {
    // -- generic ------------------------------------------------------------
    len: (on) => asSequence(on).length,
    sum: (on) => sumOf(on),
    mean: (on) => sumOf(on) / asSequence(on).length,
    min: (on) => extremeOf(on, (v, b) => v < b),
    max: (on) => extremeOf(on, (v, b) => v > b),
    argmax: (on) => argExtreme(on, (v, b) => v > b),
    argmin: (on) => argExtreme(on, (v, b) => v < b),
    first: (on) => elementOf(on, 0),
    last: (on) => elementOf(on, asSequence(on).length - 1),
    nth: (on, a) => elementOf(on, a[0]),
    slice: (on, a) => asSequence(on).slice(a[0], a[1]),
    to_list: (on) => Array.from(asSequence(on), (x) => (typeof x === 'string' ? x : num(x))),
    unique_sorted: (on) => {
      const seen = Array.from(asSequence(on), num);
      return [...new Set(seen)].sort((x, y) => x - y);
    },
    shape: (on) => [asSequence(on).length],
    contains: (on, a) => String(on).includes(a[0]),
    round: (on, a) => {
      const f = 10 ** a[0];
      return Math.round(num(on) * f) / f;
    },
    identity: (on) => on,
    count_gt: (on, a) => {
      let n = 0;
      for (const x of asSequence(on)) if (num(x) > a[0]) n++;
      return n;
    },

    // -- tttr ---------------------------------------------------------------
    'tttr.open': (on, a) => new tttrlib.TTTR(a[0], a[1]),
    'tttr.size': (on) => Number(on.size()),
    'tttr.n_valid_events': (on) => Number(on.get_n_valid_events()),
    'tttr.n_micro_channels': (on) => Number(on.get_number_of_micro_time_channels()),
    'tttr.macro_times': (on) => on.get_macro_times(),
    'tttr.micro_times': (on) => on.get_micro_times(),
    'tttr.routing_channels': (on) => on.get_routing_channel(),
    'tttr.macro_time_at': (on, a) => Number(on.get_macro_time_at(a[0])),
    'tttr.micro_time_at': (on, a) => Number(on.get_micro_time_at(a[0])),
    'tttr.routing_channel_at': (on, a) => Number(on.get_routing_channel_at(a[0])),
    'tttr.used_routing_channels': (on) => on.get_used_routing_channels(),
    'tttr.by_channel': (on, a) => on.get_tttr_by_channel(Int8Array.from(a[0])),
    'tttr.burst_search': (on, a) => on.burst_search(a[0], a[1], a[2], a[3]),
    'tttr.microtime_histogram': (on, a) => {
      const r = on.get_microtime_histogram(a[0]);
      // The multi-output getter returns [histogram, time axis].
      return Array.isArray(r) ? r[0] : r;
    },
    'tttr.header_json': (on) => on.get_header().get_json(),
    'tttr.micro_time_resolution': (on) => on.get_header().micro_time_resolution,
    'tttr.macro_time_resolution': (on) => on.get_header().macro_time_resolution,

    // -- correlator ---------------------------------------------------------
    'correlator.curve_size': (on, a) => {
      const cc = new tttrlib.CorrelatorCurve();
      cc.n_bins = a[0];
      cc.n_casc = a[1];
      return Number(cc.size());
    },

    'correlator.new': (on, a) => {
      const c = new tttrlib.Correlator();
      c.n_bins = a[0];
      c.n_casc = a[1];
      return c;
    },
    'correlator.set_tttr': (on, a) => { on.set_tttr(a[0], a[1]); },
    'correlator.x_axis': (on) => on.get_x_axis(),
    'correlator.correlation': (on) => on.get_corr_normalized(),

    // -- datastore ----------------------------------------------------------
    'ds.new': () => new tttrlib.DataStore(),
    'ds.n_rows': (on) => Number(on.n_rows()),
    'ds.n_columns': (on) => Number(on.n_columns()),
    'ds.n_groups': (on) => Number(on.n_groups()),
    'ds.column_names': (on) => Array.from(on.column_names()),
    'ds.add_group': (on, a) => on.add_group(a[0]),
    'ds.ensure_group': (on, a) => on.ensure_group(a[0]),
    'ds.group': (on, a) => on.group(a[0]),
    'ds.has_group': (on, a) => on.has_group(a[0]),
    'ds.remove_group': (on, a) => on.remove_group(a[0]),
    'ds.group_names': (on) => Array.from(on.group_names()),
    'ds.group_paths': (on) => Array.from(on.group_paths()),
    'ds.set_label': (on, a) => { on.set_label(a[0]); },
    'ds.label': (on) => on.label(),
    'ds.nbytes': (on) => Number(on.nbytes()),
    'ds.add_string': (on, a) => {
      const col = on.column(on.add_column(a[0], columnTypeValue('String')));
      for (const s of a[1]) col.push_string(s);
      if (Number(on.n_rows()) < a[1].length) on.set_n_rows(a[1].length);
    },
    'ds.column_strings': (on, a) => {
      const col = on.column_by_name(a[0]);
      const out = [];
      for (let k = 0; k < Number(col.size()); k++) out.push(col.string_at(k));
      return out;
    },
    'ds.column_dtype': (on, a) => dtypeNameOf(on.column_by_name(a[0]).type()),
    'ds.select_range': (on, a) => {
      const idx = on.find(a[0]);
      if (idx < 0) throw new Error(`no column '${a[0]}'`);
      on.select_range(idx, a[1], a[2]);
    },
    'ds.n_selected': (on) => Number(on.n_selected()),

    // -- tiff -----------------------------------------------------------------
    'tiff.write_f64': (on, a) => {
      const data = Float64Array.from(a[4]);
      tttrlib._tiff_write_f64(a[0], { data, shape: [a[1], a[2], a[3]] });
    },
    'tiff.read_f64': (on, a) => tttrlib._tiff_read_f64(a[0]),

    // -- bursts ---------------------------------------------------------------
    'burst.new': (on, a) => new tttrlib.BurstFilter(a[0]),
    'burst.find': (on) => on.find_bursts(),
    // A JS Array of Float64Array rows, not a flat block with a shape: the
    // std::vector<std::vector<double>> return has no single buffer to view.
    // List order is row order, so concatenating gives the row-major flat form
    // the other runners produce.
    'burst.properties': (on) => {
      const rows = on.get_all_burst_properties();
      const nc = rows.length ? rows[0].length : 0;
      const out = new Float64Array(rows.length * nc);
      rows.forEach((row, r) => out.set(row, r * nc));
      return out;
    },

    // -- photon selection -----------------------------------------------------
    'mask.new': (on, a) => {
      const m = new tttrlib.TTTRMask();
      m.set_tttr(a[0]);
      return m;
    },
    'mask.select_channels': (on, a) => {
      on.select_channels(a[0], Int8Array.from(a[1]), a[2]);
    },
    'mask.select_count_rate': (on, a) => { on.select_count_rate(a[0], a[1], a[2], a[3]); },
    'mask.size': (on) => Number(on.size()),
    'mask.mask_array': (on) => on.get_mask_array(),

    // -- phasor ---------------------------------------------------------------
    'phasor.g': (on, a) => tttrlib.DecayPhasor.g(a[0], a[1], a[2], a[3]),
    'phasor.s': (on, a) => tttrlib.DecayPhasor.s(a[0], a[1], a[2], a[3]),
    'phasor.from_bincounts': (on, a) =>
      Float64Array.from(tttrlib.DecayPhasor.phasor_of_bincounts(
        Int32Array.from(a[0]), a[1], a[2], a[3], a[4])),

    // -- pda ------------------------------------------------------------------
    'pda.new': (on, a) =>
      new tttrlib.Pda(a[0], a[1], a[2], a[3], Float64Array.from(a[4])),
    'pda.append': (on, a) => { on.append(a[0], a[1]); },
    'pda.evaluate': (on) => { on.evaluate(); },
    'pda.s1s2': (on) => on.get_S1S2_matrix(),
    // get_1dhistogram returns [x, y].
    'pda.histogram_y': (on) => on.get_1dhistogram()[1],

    // -- decay fitting --------------------------------------------------------
    'fit.names': () => Array.from(tttrlib.decay_fit_names()),
    'fit.setup_names': (on, a) => Array.from(tttrlib.decay_fit_setup_names(a[0])),
    'fit.result_names': (on, a) => Array.from(tttrlib.decay_fit_result_names(a[0], 0)),
    'fit.setup_vector': (on, a) =>
      Float64Array.from(tttrlib.decay_fit_setup_vector(a[0], a[1])),
    'fit.problem': (on, a) => {
      const p = new tttrlib.DecayFitProblem(a[0], a[1], a[2]);
      p.irf = Float64Array.from(a[3]);
      p.background = Float64Array.from(a[4]);
      p.data = Float64Array.from(a[5]);
      return p;
    },
    'fit.new': (on, a) =>
      new tttrlib.DecayFit2(a[0], Float64Array.from(a[1]), Float64Array.from(a[2])),
    'fit.run': (on, a) =>
      on.fit(Float64Array.from(a[0]),
             new tttrlib.DecayFitConstraints(Int32Array.from(a[1])), a[2]),
    'fit.objective': (on) => on.objective,
    'fit.parameters': (on) => Float64Array.from(on.parameters),
    'fit.results': (on) => Float64Array.from(on.results),

    // -- clsm ---------------------------------------------------------------------
    // The addon's later constructor parameters have C++ defaults, but SWIG's
    // Node-API dispatch matches on argument count, so all five are passed.
    'clsm.open': (on, a) =>
      new tttrlib.CLSMImage(a[0], new tttrlib.CLSMSettings(), null, true,
                            Int32Array.from(a[1])),
    'clsm.n_frames': (on) => Number(on.n_frames),
    'clsm.n_lines': (on) => Number(on.n_lines),
    'clsm.n_pixel': (on) => Number(on.n_pixel),
    'clsm.intensity': (on) => on.get_intensity(),
    'clsm.mean_micro_time': (on, a) => on.get_mean_micro_time(a[0]),
    'clsm.decay_of_pixels': (on, a) => {
      const [f0, l0, l1, p0, p1] = a[1];
      const nf = Number(on.n_frames), nl = Number(on.n_lines), np = Number(on.n_pixel);
      const mask = new Uint8Array(nf * nl * np);
      for (let f = f0; f < nf; f++)
        for (let l = l0; l < l1; l++)
          for (let p = p0; p < p1; p++) mask[(f * nl + l) * np + p] = 1;
      return Float64Array.from(
        on.get_decay_of_pixels_v(a[0], { data: mask, shape: [nf, nl, np] },
                                 a[2], a[3]));
    },
    'clsm.fluorescence_decay': (on, a) =>
      Float64Array.from(on.get_fluorescence_decay_v(a[0], a[1], a[2])),

    // -- histogram --------------------------------------------------------------
    // jsarrays.i takes 2-D input as {data, shape}; a bare flat TypedArray would
    // need a shape it does not carry.
    'hist.new': () => new tttrlib.doubleHistogram(),
    'hist.set_axis': (on, a) => { on.set_axis(a[0], a[1], a[2], a[3], a[4], a[5]); },
    'hist.update': (on, a) => {
      const values = Float64Array.from(a[0]);
      on.update({ data: values, shape: [values.length, 1] });
    },
    'hist.counts': (on) => on.get_histogram(),

    // -- bitmask ---------------------------------------------------------------
    'bitmask.new': (on, a) => new tttrlib.BitMask(a[0]),
    'bitmask.set': (on, a) => { on.set(a[0], a[1]); },
    'bitmask.size': (on) => Number(on.size()),
    'bitmask.count': (on) => Number(on.count()),
    'bitmask.to_bytes': (on) => {
      const out = new Uint8Array(Number(on.size()));
      on.to_bytes(out);
      return out;
    },

    // -- registry -------------------------------------------------------------
    'registry.json': () => tttrlib.registry_json(),
    'registry.category_json': (on, a) => tttrlib.registry_category_json(a[0]),
    'registry.categories': () => Array.from(tttrlib.registry_categories()),

    // -- files ---------------------------------------------------------------
    'file.write_text': (on, a) => { writeFileSync(a[0], a[1], 'utf8'); },

    // -- hdf5 ----------------------------------------------------------------
    'hdf5.write': (on, a) =>
      tttrlib.write_hdf5_table(a[0], a[1], a[2], 0, tttrlib.Hdf5WriteMode_Update),
    'hdf5.read': (on, a) => {
      const s = new tttrlib.DataStore();
      tttrlib.read_hdf5_table_into(s, a[0], a[1], true);
      return s;
    },
    'hdf5.groups': (on, a) => Array.from(tttrlib.hdf5_table_groups(a[0])),
    'hdf5.has': (on, a) => tttrlib.hdf5_table_has(a[0], a[1]),
  };

  for (const sfx of Object.keys(COLUMN_TYPES)) {
    ops[`ds.add_${sfx}`] = addTyped(sfx);
    ops[`ds.column_${sfx}`] = columnTyped(sfx);
  }
  return ops;
}

function elementOf(on, k) {
  const a = asSequence(on);
  const v = a[k];
  return typeof v === 'string' ? v : num(v);
}

/** The TypedArray a set_<sfx> call expects, built from plain numbers. */
function makeTyped(sfx, values) {
  switch (sfx) {
    case 'f64': return Float64Array.from(values);
    case 'f32': return Float32Array.from(values);
    case 'i64': return BigInt64Array.from(values, (v) => BigInt(Math.round(v)));
    case 'u64': return BigUint64Array.from(values, (v) => BigInt(Math.round(v)));
    case 'i32': return Int32Array.from(values);
    case 'u32': return Uint32Array.from(values);
    case 'i16': return Int16Array.from(values);
    case 'u16': return Uint16Array.from(values);
    case 'i8': return Int8Array.from(values);
    case 'u8': return Uint8Array.from(values);
    default: throw new Error(`no setter for ${sfx}`);
  }
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

function compare(expected, actual, tol) {
  if (Array.isArray(expected)) {
    const act = isTypedArray(actual) || Array.isArray(actual) ? Array.from(actual) : null;
    if (act === null) return `expected a list of ${expected.length}, got ${actual}`;
    if (act.length !== expected.length)
      return `expected ${expected.length} elements, got ${act.length}`;
    for (let k = 0; k < act.length; k++) {
      const why = compare(expected[k], act[k], tol);
      if (why) return `[${k}]: ${why}`;
    }
    return null;
  }
  if (typeof expected === 'boolean') {
    if (typeof actual !== 'boolean' || expected !== actual)
      return `expected ${expected}, got ${actual}`;
    return null;
  }
  if (typeof expected === 'string') {
    return expected === actual ? null : `expected '${expected}', got '${actual}'`;
  }
  const v = typeof actual === 'bigint' ? Number(actual) : actual;
  if (typeof v !== 'number') return `expected a number, got ${actual}`;
  if (!tol) return expected === v ? null : `expected ${expected}, got ${v}`;
  const scale = Math.max(Math.abs(expected), Math.abs(v), 1e-300);
  if (Math.abs(expected - v) <= tol * scale) return null;
  return `expected ${expected}, got ${v} `
       + `(relative error ${Math.abs(expected - v) / scale} > ${tol})`;
}

/** Refuse an exact integer expectation a double could not hold. */
function checkComparable(c) {
  const tol = c.tolerance || {};
  for (const [key, value] of Object.entries(c.expect)) {
    if (tol[key]) continue;
    for (const v of [value].flat(Infinity))
      if (typeof v === 'number' && Math.abs(v) >= MAX_EXACT_INTEGER)
        throw new Error(`${c.id}: expected integer ${key}=${v} is at or above 2^53, `
                      + 'which JavaScript cannot compare exactly');
  }
}

// ---------------------------------------------------------------------------
// The case loop
// ---------------------------------------------------------------------------

function runCase(c) {
  const bindings = new Map();
  const dataFiles = c.data || [];
  const tmpDir = mkdtempSync(path.join(tmpdir(), 'tttrlib-conf-'));
  const tmpPaths = Array.from({ length: c.tmp || 0 },
                              (_, k) => path.join(tmpDir, `scratch${k}`));
  const ops = makeOps();

  const resolve = (x) => {
    if (Array.isArray(x)) return x.map(resolve);
    if (typeof x === 'string' && x.startsWith('$')) {
      const name = x.slice(1);
      let m = /^data(\d+)$/.exec(name);
      if (m) return dataPath(dataFiles[Number(m[1])]);
      m = /^tmp(\d+)$/.exec(name);
      if (m) return tmpPaths[Number(m[1])];
      if (!bindings.has(name)) throw new Error(`step reads unbound '${name}'`);
      return bindings.get(name);
    }
    return x;
  };

  try {
    for (const step of c.steps) {
      const fn = ops[step.op];
      if (!fn) throw new UnsupportedOp(step.op);
      const args = (step.args || []).map(resolve);
      if (step.on !== undefined && !bindings.has(step.on))
        throw new Error(`step reads unbound '${step.on}'`);
      const on = step.on === undefined ? undefined : bindings.get(step.on);

      if (step.throws) {
        let threw;
        try { fn(on, args); threw = false; }
        catch (e) { if (e instanceof UnsupportedOp) throw e; threw = true; }
        if (step.as) bindings.set(step.as, threw);
        continue;
      }
      const result = fn(on, args);
      if (step.as) {
        if (bindings.has(step.as)) throw new Error(`rebinds '${step.as}'`);
        bindings.set(step.as, result);
      }
    }

    const tol = c.tolerance || {};
    const failures = [];
    for (const [key, expected] of Object.entries(c.expect)) {
      if (!bindings.has(key)) { failures.push(`${key}: the steps never bound it`); continue; }
      const why = compare(expected, bindings.get(key), tol[key] || 0);
      if (why) failures.push(`${key}: ${why}`);
    }
    return failures;
  } finally {
    rmSync(tmpDir, { recursive: true, force: true });
  }
}

// ---------------------------------------------------------------------------
// Wiring into node:test
// ---------------------------------------------------------------------------

// Keyed by case id, not appended: a case must appear exactly once in the
// report whatever the runner does around it, and the matrix reads a status per
// case. The skip/unsupported entries are seeded before the run so a case whose
// body never executes still has a row -- "did not run" and "not reported" must
// not look the same.
const report = new Map();
const record = (id, area, status, reason = '') => report.set(id, { id, area, status, reason });

// A process exit hook rather than node:test's `after`: a top-level `after` in a
// test file does not fire in every node:test version, and a missing report is
// indistinguishable in the matrix from a runner that never ran. writeFileSync is
// synchronous, which is what makes it safe here.
process.on('exit', () => {
  const out = process.env.TTTRLIB_CONFORMANCE_REPORT;
  if (out) writeFileSync(out, JSON.stringify(
    { language: 'js', results: [...report.values()] }, null, 2));
});

const files = existsSync(CASES_DIR)
  ? readdirSync(CASES_DIR).filter((f) => f.endsWith('.json')).sort()
  : [];

for (const file of files) {
  const doc = JSON.parse(readFileSync(path.join(CASES_DIR, file), 'utf8'));
  describe(`conformance: ${doc.area}`, () => {
    for (const c of doc.cases) {
      const why = (c.unsupported || {}).js;
      const missing = (c.data || []).filter((d) => !existsSync(path.join(DATA_ROOT, d)));
      const skip = why ? `declared unsupported in JavaScript: ${why}`
                       : missing.length ? `test data not present: ${missing.join(', ')}`
                       : false;
      if (why) record(c.id, doc.area, 'unsupported', why);
      else if (missing.length)
        record(c.id, doc.area, 'skip', `missing ${missing.join(', ')}`);

      test(c.id, { skip }, () => {
        checkComparable(c);
        let failures;
        try {
          failures = runCase(c);
        } catch (e) {
          if (e instanceof UnsupportedOp) {
            record(c.id, doc.area, 'unsupported', `op '${e.op}' not implemented`);
            return;
          }
          record(c.id, doc.area, 'fail', e.message);
          throw e;
        }
        if (failures.length) {
          record(c.id, doc.area, 'fail', failures.join('; '));
          assert.fail(`${c.id}\n  ${failures.join('\n  ')}`);
        }
        record(c.id, doc.area, 'pass');
      });
    }
  });
}
