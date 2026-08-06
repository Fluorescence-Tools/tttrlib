// SPDX-License-Identifier: BSD-3-Clause
//
// The JavaScript counterpart of test/python/test_settings.py: one place that
// knows where the addon and the test data are, so no test file hard-codes a
// path. Reads the SAME test/settings.json the Python, R and Java suites read.

import { createRequire } from 'node:module';
import { readFileSync, existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const HERE = path.dirname(fileURLToPath(import.meta.url));
const TEST_ROOT = path.dirname(HERE);
const REPO_ROOT = path.dirname(TEST_ROOT);

export const settings = JSON.parse(
  readFileSync(path.join(TEST_ROOT, 'settings.json'), 'utf8'));

// Same override precedence as the Python loader: $TTTRLIB_DATA wins, then the
// data_root from settings.json, resolved relative to the repository root.
const envRoot = (process.env.TTTRLIB_DATA || '').trim().replace(/^['"]|['"]$/g, '');
export const DATA_ROOT = envRoot
  ? path.resolve(envRoot)
  : path.resolve(REPO_ROOT, settings.data_root || 'tttr-data');

export const DATA_AVAILABLE = existsSync(DATA_ROOT);

/** Absolute path of a data file named the way settings.json names it. */
export function dataPath(relative) {
  return path.join(DATA_ROOT, relative);
}

/** True when a specific data file is present -- for per-test skipping. */
export function hasData(relative) {
  return DATA_AVAILABLE && existsSync(dataPath(relative));
}

// Load the package (which finds the addon). $TTTRLIB_JS_PKG points at a staged
// package directory; otherwise use the one in the source tree, whose loader
// already searches the build directory.
const pkgDir = process.env.TTTRLIB_JS_PKG
  || path.join(REPO_ROOT, 'ext', 'js', 'pkg');

export const tttrlib = require(path.join(pkgDir, 'index.js'));
