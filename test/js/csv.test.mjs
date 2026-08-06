// SPDX-License-Identifier: BSD-3-Clause
//
// The CSV entry points, from JavaScript.
//
// The formatting and the escaping are C++ and are covered by the Python suite
// against pyarrow; what is only exercised here is the scripting layer in
// ext/js/pkg/index.js -- the options object it fills, and in particular the
// list-valued options, which a SWIG std::vector<std::string> parameter does not
// accept as a plain JavaScript array. That failed silently in readCsv before
// writeCsv arrived and needed the same conversion.

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { tttrlib } from './settings.mjs';

function store() {
  const s = new tttrlib.DataStore();
  s.set_n_rows(3);
  const i = s.add_column('i', tttrlib.ColumnType_Int32);
  s.column(i).set_i32(new Int32Array([1, 2, 3]));
  const x = s.add_column('x', tttrlib.ColumnType_Float64);
  s.column(x).set_f64(new Float64Array([0.1, 2.5, 1 / 4]));
  const label = s.add_column('label', tttrlib.ColumnType_String);
  for (const v of ['a,b', 'plain', 'x']) s.column(label).push_string(v);
  return s;
}

describe('writeCsv', () => {

  test('writes the table, quoting only what has to be', () => {
    assert.equal(tttrlib.writeCsv(null, store()),
      'i,x,label\n1,0.1,"a,b"\n2,2.5,plain\n3,0.25,x\n');
  });

  test('columns selects and orders them', () => {
    assert.equal(tttrlib.writeCsv(null, store(), { columns: ['label', 'i'], header: false }),
      '"a,b",1\nplain,2\nx,3\n');
  });

  test('quoting all wraps the numbers too', () => {
    const text = tttrlib.writeCsv(null, store(), { quoting: 'all', columns: ['i'] });
    assert.equal(text, '"i"\n"1"\n"2"\n"3"\n');
  });

  test('a file round-trips through readCsv', () => {
    const file = path.join(fs.mkdtempSync(path.join(os.tmpdir(), 'tttrlib-csv-')), 'w.csv');
    assert.equal(tttrlib.writeCsv(file, store()), true);

    const back = tttrlib.readCsv(file, { textColumns: ['label'] });
    assert.equal(back.n_rows(), 3);
    assert.equal(back.n_columns(), 3);
    assert.equal(back.column(2).string_at(0), 'a,b');
    assert.equal(back.column(1).value_at(0), 0.1);
  });

  test('a bad column name throws rather than writing half a file', () => {
    assert.throws(() => tttrlib.writeCsv(null, store(), { columns: ['nope'] }));
  });
});
