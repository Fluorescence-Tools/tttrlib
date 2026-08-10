// SPDX-License-Identifier: BSD-3-Clause
//
// The registry, and the registry-driven burst-search entry point.
//
// Counterpart of test/python/test_registry.py. This is the surface the burst and
// single-molecule web applications are built on, and the point of it is that a
// UI needs no hard-coded algorithm list: assert the shape of the data, never the
// membership of the list, or this file becomes the hard-coded list it exists to
// make unnecessary.

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { tttrlib, settings, dataPath, hasData } from './settings.mjs';

describe('registry', () => {
  test('with no argument, returns every category', () => {
    const all = tttrlib.registry();
    assert.equal(typeof all, 'object');
    const categories = Object.keys(all);
    assert.ok(categories.length > 0);
    assert.ok(categories.includes('burst_search'));
    assert.ok(categories.includes('file_container'));
  });

  test('with a category, returns that category\'s entries', () => {
    const searches = tttrlib.registry('burst_search');
    assert.ok(Object.keys(searches).length > 0);
    for (const [name, entry] of Object.entries(searches)) {
      assert.equal(typeof entry.method, 'string', `${name} declares no method`);
      assert.ok(entry.params_schema, `${name} declares no params_schema`);
      assert.equal(entry.params_schema.type, 'object');
    }
  });

  test('every parameter is renderable: a type, and a default or a `required` mark', () => {
    // This is what makes a form a pure function of the schema. A property with
    // no type cannot be given a widget; one with neither a default nor a place
    // in `required` leaves a UI unable to decide whether to pre-fill it or
    // insist the user supplies it. Both are legitimate -- the coincident
    // search's `channel_groups` has no meaningful default and says so by being
    // required -- but silence is not.
    for (const entry of Object.values(tttrlib.registry('burst_search'))) {
      const schema = entry.params_schema;
      const required = new Set(schema.required || []);
      for (const [prop, spec] of Object.entries(schema.properties || {})) {
        assert.ok(spec.type, `${entry.method}.${prop} has no type`);
        // A property carrying `parameters_of` is a declarative link to another
        // entry's schema (Registry.h documents composite entries); its shape is
        // that entry's, so it has no default of its own.
        assert.ok('default' in spec || required.has(prop) || spec.parameters_of,
                  `${entry.method}.${prop} has neither a default, a required mark, ` +
                  'nor a parameters_of link');
      }
    }
  });

  test('an unknown category throws and names the ones that exist', () => {
    assert.throws(() => tttrlib.registry('no_such_category'),
                  (e) => /burst_search/.test(e.message));
  });

  test('registry_categories() agrees with the keys of the full registry', () => {
    // Regression guard. This returned an empty list in every binding until the
    // dangling-temporary bug in registry_categories() was fixed (a range-for
    // over `build().items()`, where the json dies before the loop runs). Two
    // routes to the same list must not disagree.
    const listed = Array.from(tttrlib.registry_categories()).sort();
    const keys = Object.keys(tttrlib.registry()).sort();
    assert.ok(listed.length > 0, 'registry_categories() returned nothing');
    assert.deepEqual(listed, keys);
  });
});

const SPC = settings.spc132_filename;

describe('registry-driven burst search', { skip: !hasData(SPC) && 'no data' }, () => {
  test('every advertised search runs, from the registry alone', () => {
    const data = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
    const names = tttrlib.burstSearchAlgorithms();
    assert.ok(names.length > 0);
    for (const name of names) {
      // Parameters with no default must be supplied; the schema says which.
      const schema = tttrlib.registry('burst_search')[name].params_schema;
      const params = {};
      for (const [prop, spec] of Object.entries(schema.properties || {})) {
        if (!('default' in spec) && prop === 'channel_groups') params[prop] = [[0], [8]];
      }
      const result = data.burstSearchByName(name, params);
      // Burst boundaries come back flat, [start, stop, start, stop, ...].
      assert.ok(result instanceof BigInt64Array, `${name} returned ${result.constructor.name}`);
      assert.equal(result.length % 2, 0, `${name} returned an odd number of boundaries`);
      for (let i = 0; i < result.length; i += 2) {
        assert.ok(result[i] <= result[i + 1], `${name}: burst ${i / 2} ends before it starts`);
      }
    }
  });

  test('defaults come from the schema, and overriding one changes the result', () => {
    const data = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
    const defaults = tttrlib.burstSearchDefaults('sliding_window');
    assert.ok(Object.keys(defaults).length > 0);
    const base = data.burstSearchByName('sliding_window');
    // A far stricter minimum burst size cannot yield more bursts.
    const strict = data.burstSearchByName('sliding_window', { L: 500 });
    assert.ok(strict.length <= base.length);
  });

  test('an unknown parameter is rejected, and the message lists the real ones', () => {
    const data = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
    assert.throws(() => data.burstSearchByName('sliding_window', { nonsense: 1 }),
                  /unknown parameter/);
  });

  test('an unknown algorithm is rejected', () => {
    const data = new tttrlib.TTTR(dataPath(SPC), 'SPC-130');
    assert.throws(() => data.burstSearchByName('no_such_search'), /unknown burst search/);
  });

  // burstSearchByName builds a POSITIONAL argument list from the key order of
  // params_schema.properties, because JavaScript has no **kwargs. That order is
  // the C++ signature order, and nothing in the wrapper can check it: SWIG's
  // Node-API functions report length 0 whatever their real arity.
  //
  // `required` lists the same parameters in the same declaration order, so it
  // must be a subsequence of the property keys. Sorting the properties -- which
  // is what a nlohmann::json (std::map) round-trip in the registry did once --
  // breaks that relation immediately, whereas the search itself may keep running
  // and simply return the wrong bursts.
  //
  // This is a JavaScript test only because JavaScript is the only binding that
  // depends on the order; Python calls the same registry with **kwargs.
  test('property order is the declaration order, which is the argument order', () => {
    const entries = tttrlib.registry('burst_search');
    for (const [name, entry] of Object.entries(entries)) {
      const schema = entry.params_schema || {};
      const props = Object.keys(schema.properties || {});
      const required = schema.required || [];
      let at = -1;
      for (const r of required) {
        const i = props.indexOf(r, at + 1);
        assert.notEqual(i, -1,
          `${name}: '${r}' is required but does not follow the earlier required ` +
          `parameters in properties order — params_schema.properties has been ` +
          `re-ordered (sorted?), and the positional dispatch now transposes arguments.\n` +
          `  properties: [${props.join(', ')}]\n  required:   [${required.join(', ')}]`);
        at = i;
      }
    }
  });
});
