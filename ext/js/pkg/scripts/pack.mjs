#!/usr/bin/env node
// SPDX-License-Identifier: BSD-3-Clause
//
// Assemble the publishable npm package from per-platform prebuilds.
//
//   node scripts/pack.mjs [--prebuilds <dir>] [--out <dir>] [--version X.Y.Z]
//                         [--require-triples darwin-arm64,linux-x64,...]
//
// CI runs scripts/prebuild.mjs on five runners, uploads each
// prebuilds/<triple>/ as an artifact, downloads them all onto one machine and
// runs this. The result is a directory `npm pack`/`npm publish` can be pointed
// at directly.
//
// The version is read from pyproject.toml rather than kept in package.json.
// tttrlib already treats pyproject.toml as the single source of truth for the
// version (CMakeLists.txt parses it too); a hand-maintained version in
// package.json is a fifth place to forget on a release.

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const PKG_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const REPO_ROOT = path.resolve(PKG_DIR, '..', '..', '..');

// The five platforms PRD-016 M5 commits to. Named here rather than inferred
// from whatever happens to be in the download directory: a runner that silently
// failed leaves a gap, and a package published with four of five prebuilds is
// indistinguishable from a complete one until a user on the missing platform
// installs it.
const DEFAULT_TRIPLES = ['darwin-arm64', 'darwin-x64', 'linux-x64', 'linux-arm64', 'win32-x64'];

function parseArgs(argv) {
  const opts = {
    prebuilds: path.join(PKG_DIR, 'prebuilds'),
    out: path.join(REPO_ROOT, 'npm-package'),
    version: null,
    require: DEFAULT_TRIPLES,
    pack: false,
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--prebuilds') opts.prebuilds = path.resolve(argv[++i]);
    else if (a === '--out') opts.out = path.resolve(argv[++i]);
    else if (a === '--version') opts.version = argv[++i];
    else if (a === '--require-triples') opts.require = argv[++i].split(',').filter(Boolean);
    else if (a === '--allow-partial') opts.require = [];
    else if (a === '--pack') opts.pack = true;
    else throw new Error(`pack: unknown argument ${a}`);
  }
  return opts;
}

function versionFromPyproject() {
  const text = fs.readFileSync(path.join(REPO_ROOT, 'pyproject.toml'), 'utf8');
  const m = /\nversion\s*=\s*"([^"]+)"/.exec(text);
  if (!m) throw new Error('pack: no version in pyproject.toml');
  return m[1];
}

function copyDir(src, dst) {
  fs.mkdirSync(dst, { recursive: true });
  for (const e of fs.readdirSync(src, { withFileTypes: true })) {
    const s = path.join(src, e.name);
    const d = path.join(dst, e.name);
    if (e.isDirectory()) copyDir(s, d);
    else fs.copyFileSync(s, d);
  }
}

function main() {
  const opts = parseArgs(process.argv.slice(2));
  const version = opts.version || versionFromPyproject();

  if (!fs.existsSync(opts.prebuilds)) {
    throw new Error(`pack: no prebuilds directory at ${opts.prebuilds}`);
  }
  const found = fs.readdirSync(opts.prebuilds, { withFileTypes: true })
    .filter((e) => e.isDirectory())
    .map((e) => e.name)
    .sort();

  // A triple directory with no .node in it is the failure mode that a plain
  // existence check misses: an artifact download that produced an empty
  // directory looks like a present platform.
  const usable = found.filter((t) =>
    fs.readdirSync(path.join(opts.prebuilds, t)).some((f) => f.endsWith('.node')));
  const missing = opts.require.filter((t) => !usable.includes(t));
  if (missing.length) {
    throw new Error(
      `pack: no usable prebuild for ${missing.join(', ')}\n` +
      `  found: ${found.length ? found.map((t) => `${t}${usable.includes(t) ? '' : ' (no .node)'}`).join(', ') : 'nothing'}\n` +
      '  Pass --allow-partial only for a local dry run, never for a publish.');
  }

  fs.rmSync(opts.out, { recursive: true, force: true });
  fs.mkdirSync(opts.out, { recursive: true });

  for (const f of ['index.js', 'index.d.ts', 'README.md']) {
    fs.copyFileSync(path.join(PKG_DIR, f), path.join(opts.out, f));
  }
  fs.copyFileSync(path.join(REPO_ROOT, 'LICENSE.txt'), path.join(opts.out, 'LICENSE.txt'));
  for (const t of usable) copyDir(path.join(opts.prebuilds, t), path.join(opts.out, 'prebuilds', t));

  const pkg = JSON.parse(fs.readFileSync(path.join(PKG_DIR, 'package.json'), 'utf8'));
  pkg.version = version;
  // devDependencies are the SWIG/compile-time headers; a consumer needs none of
  // them, and node_modules/ has no business in a package that ships binaries.
  delete pkg.devDependencies;
  delete pkg.scripts;
  fs.writeFileSync(path.join(opts.out, 'package.json'), JSON.stringify(pkg, null, 2) + '\n');

  let bytes = 0;
  const walk = (d) => {
    for (const e of fs.readdirSync(d, { withFileTypes: true })) {
      const p = path.join(d, e.name);
      if (e.isDirectory()) walk(p); else bytes += fs.statSync(p).size;
    }
  };
  walk(opts.out);

  console.log(`tttrlib@${version} assembled in ${opts.out}`);
  console.log(`  platforms: ${usable.join(', ')}`);
  console.log(`  unpacked:  ${(bytes / 1048576).toFixed(1)} MB`);

  if (opts.pack) {
    const r = spawnSync('npm', ['pack'], { cwd: opts.out, stdio: 'inherit' });
    if (r.status !== 0) throw new Error('npm pack failed');
  }
}

main();
