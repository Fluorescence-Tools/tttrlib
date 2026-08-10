// SPDX-License-Identifier: BSD-3-Clause
//
// The published package resolves its binary differently from the source tree:
// index.js hands off to `node-gyp-build` when a prebuilds/ directory exists, and
// falls back to searching build*/js-pkg/ when it does not. Only the second half
// of that runs during ordinary development, so the first half is exactly the
// kind of code that is discovered to be broken by a user rather than by CI.
//
// These tests build a throwaway package directory out of the addon this suite is
// already testing, laid out the way scripts/pack.mjs lays out a release, and
// require() it in a child process. That covers the naming convention
// (`node.napi[.glibc].node`), the tag matching, and the branch in index.js --
// without needing five platforms' binaries.
//
// The child process is not a stylistic choice: a loader failure in a native
// addon aborts the process often enough that catching it in this one would turn
// a red test into a dead runner.

import { test, describe } from 'node:test';
import assert from 'node:assert/strict';
import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { createRequire } from 'node:module';
import { fileURLToPath } from 'node:url';

import { tttrlib } from './settings.mjs';

const require = createRequire(import.meta.url);
const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.dirname(path.dirname(HERE));
const PKG_DIR = path.join(REPO_ROOT, 'ext', 'js', 'pkg');

// The addon currently under test -- whatever settings.mjs's loader picked.
function currentAddonPath() {
  const loaded = process.env.TTTRLIB_ADDON;
  if (loaded && fs.existsSync(loaded)) return path.resolve(loaded);
  const candidates = [
    path.join(PKG_DIR, 'tttrlib.node'),
    ...fs.readdirSync(REPO_ROOT, { withFileTypes: true })
      .filter((e) => e.isDirectory() && /^(build|cmake-build)/.test(e.name))
      .map((e) => path.join(REPO_ROOT, e.name, 'js-pkg', 'tttrlib.node'))
      .filter((p) => fs.existsSync(p))
      .sort((a, b) => fs.statSync(b).mtimeMs - fs.statSync(a).mtimeMs),
  ];
  return candidates.find((p) => fs.existsSync(p)) || null;
}

// The file name node-gyp-build looks for on this platform. Mirrors
// scripts/prebuild.mjs; if the two ever disagree, this test fails and the
// release does not silently ship an unloadable directory.
function prebuildFileName() {
  if (os.platform() !== 'linux') return 'node.napi.node';
  return fs.existsSync('/etc/alpine-release') ? 'node.napi.musl.node' : 'node.napi.glibc.node';
}

// A SHARED-module build needs its libtttrlib_* siblings; a STATIC one has none.
// Copy whatever sits beside the addon so both shapes are exercised as they are.
function stagePrebuild(dest, addon) {
  const triple = `${os.platform()}-${os.arch()}`;
  const dir = path.join(dest, 'prebuilds', triple);
  fs.mkdirSync(dir, { recursive: true });
  const srcDir = path.dirname(addon);
  for (const f of fs.readdirSync(srcDir)) {
    if (/\.(dylib|so|dll)($|\.)/.test(f)) fs.copyFileSync(path.join(srcDir, f), path.join(dir, f));
  }
  fs.copyFileSync(addon, path.join(dir, prebuildFileName()));
  for (const f of ['index.js', 'index.d.ts', 'package.json']) {
    fs.copyFileSync(path.join(PKG_DIR, f), path.join(dest, f));
  }
  // node-gyp-build is a runtime dependency of the published package; the staged
  // copy reaches the real one through the source tree's node_modules.
  fs.mkdirSync(path.join(dest, 'node_modules'), { recursive: true });
  fs.symlinkSync(
    path.join(PKG_DIR, 'node_modules', 'node-gyp-build'),
    path.join(dest, 'node_modules', 'node-gyp-build'),
    'dir');
  return dir;
}

// PREBUILDS_ONLY stops node-gyp-build from falling back to build/Release, and
// TTTRLIB_ADDON is cleared so the staged package cannot accidentally be answered
// by the developer's build tree -- which is what would make this test pass
// without ever reading prebuilds/.
function requireStaged(dest, expr) {
  return spawnSync(process.execPath, ['-e',
    `const t = require(${JSON.stringify(dest)}); ${expr}`], {
    encoding: 'utf8',
    env: { ...process.env, PREBUILDS_ONLY: '1', TTTRLIB_ADDON: '' },
  });
}

const addon = currentAddonPath();
const skip = addon ? false : 'no built addon to stage';

describe('packaging: the published prebuilds layout', () => {
  test('a staged package loads through node-gyp-build', { skip }, () => {
    const dest = fs.mkdtempSync(path.join(os.tmpdir(), 'tttrlib-pkg-'));
    try {
      stagePrebuild(dest, addon);
      const r = requireStaged(dest,
        'console.log(JSON.stringify({areas: Object.keys(t.registry()).length, zc: t.arraysAreZeroCopy()}))');
      assert.equal(r.status, 0, `staged package failed to load:\n${r.stderr}`);
      const out = JSON.parse(r.stdout.trim().split('\n').pop());
      // A binary whose registry is empty loaded but is useless: with STATIC
      // modules that is what a dropped static initialiser looks like, and it is
      // the specific way a prebuild can be broken while still being loadable.
      assert.ok(out.areas > 0, 'registry() is empty in the staged package');
      assert.equal(typeof out.zc, 'boolean');
    } finally {
      fs.rmSync(dest, { recursive: true, force: true });
    }
  });

  test('the staged package really used prebuilds/, not a build tree', { skip }, () => {
    const dest = fs.mkdtempSync(path.join(os.tmpdir(), 'tttrlib-pkg-'));
    try {
      const dir = stagePrebuild(dest, addon);
      const r = requireStaged(dest, 'console.log("loaded")');
      assert.equal(r.status, 0, r.stderr);
      // Remove the binary and the same require() must now fail. Without this,
      // the test above would pass just as happily against a loader that ignored
      // prebuilds/ and found the addon somewhere else.
      fs.rmSync(path.join(dir, prebuildFileName()));
      const r2 = requireStaged(dest, 'console.log("loaded")');
      assert.notEqual(r2.status, 0, 'package loaded with no binary in prebuilds/');
      assert.match(r2.stderr, /No native build was found|Cannot find module/);
    } finally {
      fs.rmSync(dest, { recursive: true, force: true });
    }
  });

  test('a foreign-platform prebuild is not loaded', { skip }, () => {
    const dest = fs.mkdtempSync(path.join(os.tmpdir(), 'tttrlib-pkg-'));
    try {
      const dir = stagePrebuild(dest, addon);
      // Rename this platform's directory to one that cannot match. node-gyp-build
      // must refuse rather than load the only binary it can see -- loading a
      // foreign binary is a process abort, not an exception, so "it found
      // something" is the dangerous outcome here.
      fs.renameSync(dir, path.join(dest, 'prebuilds', 'aix-mips64'));
      const r = requireStaged(dest, 'console.log("loaded")');
      assert.notEqual(r.status, 0);
      assert.match(r.stderr, /No native build was found/);
    } finally {
      fs.rmSync(dest, { recursive: true, force: true });
    }
  });
});

describe('packaging: metadata', () => {
  test('package.json declares what the loader and the packer rely on', () => {
    const pkg = require(path.join(PKG_DIR, 'package.json'));
    assert.equal(pkg.main, 'index.js');
    assert.equal(pkg.types, 'index.d.ts');
    // node-gyp-build is what index.js require()s in an installed package; the
    // header packages are build-time only and must not follow the binary into
    // every consumer's node_modules.
    assert.ok(pkg.dependencies['node-gyp-build'], 'node-gyp-build must be a runtime dependency');
    assert.ok(!pkg.dependencies['node-addon-api'], 'node-addon-api is build-time only');
    assert.ok(!pkg.dependencies['node-api-headers'], 'node-api-headers is build-time only');
    assert.ok(pkg.files.includes('prebuilds/'), 'the published tarball must carry prebuilds/');
  });

  test('the packer refuses an incomplete set of platforms', () => {
    // The failure this guards is silent by nature: a package assembled from four
    // of five prebuilds installs and works everywhere except the platform whose
    // runner failed.
    const empty = fs.mkdtempSync(path.join(os.tmpdir(), 'tttrlib-prebuilds-'));
    try {
      fs.mkdirSync(path.join(empty, 'darwin-arm64'), { recursive: true });
      const r = spawnSync(process.execPath,
        [path.join(PKG_DIR, 'scripts', 'pack.mjs'), '--prebuilds', empty,
          '--out', path.join(empty, 'out'), '--version', '0.0.0-test'],
        { encoding: 'utf8' });
      assert.notEqual(r.status, 0, 'pack.mjs accepted a prebuilds directory with no binaries');
      assert.match(r.stderr, /no usable prebuild/);
    } finally {
      fs.rmSync(empty, { recursive: true, force: true });
    }
  });

  test('the version in a packed package comes from pyproject.toml', () => {
    const pyproject = fs.readFileSync(path.join(REPO_ROOT, 'pyproject.toml'), 'utf8');
    const expected = /\nversion\s*=\s*"([^"]+)"/.exec(pyproject)[1];
    const src = fs.mkdtempSync(path.join(os.tmpdir(), 'tttrlib-prebuilds-'));
    const out = fs.mkdtempSync(path.join(os.tmpdir(), 'tttrlib-out-'));
    try {
      const triple = `${os.platform()}-${os.arch()}`;
      fs.mkdirSync(path.join(src, triple), { recursive: true });
      fs.writeFileSync(path.join(src, triple, prebuildFileName()), 'not a real binary');
      const r = spawnSync(process.execPath,
        [path.join(PKG_DIR, 'scripts', 'pack.mjs'), '--prebuilds', src, '--out', out,
          '--require-triples', triple],
        { encoding: 'utf8' });
      assert.equal(r.status, 0, r.stderr);
      const packed = JSON.parse(fs.readFileSync(path.join(out, 'package.json'), 'utf8'));
      assert.equal(packed.version, expected);
      assert.ok(!packed.devDependencies, 'devDependencies must be stripped from the published package');
      assert.ok(fs.existsSync(path.join(out, 'LICENSE.txt')));
      assert.ok(fs.existsSync(path.join(out, 'index.d.ts')));
    } finally {
      fs.rmSync(src, { recursive: true, force: true });
      fs.rmSync(out, { recursive: true, force: true });
    }
  });
});

// A sanity check that the suite is testing a real binding at all, so a
// catastrophic loader regression cannot leave the file above passing quietly.
test('the addon under test is loaded', () => {
  assert.equal(typeof tttrlib.registry, 'function');
});
