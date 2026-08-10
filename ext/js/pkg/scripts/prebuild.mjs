#!/usr/bin/env node
// SPDX-License-Identifier: BSD-3-Clause
//
// Build one npm prebuild for the platform this runs on.
//
//   node scripts/prebuild.mjs [--build <dir>] [--out <dir>] [--jobs N]
//                             [--no-configure] [--skip-verify]
//
// Produces  prebuilds/<platform>-<arch>/node.napi[.<libc>].node  -- the layout
// `node-gyp-build` resolves, and the layout index.js loads from when the package
// is installed rather than run out of a source tree.
//
// Two things this does that a plain `cp build/js-pkg/tttrlib.node` does not, and
// both of them are the reason it exists:
//
//   1. It installs (`cmake --install --component js`) instead of copying. A
//      build-tree binary carries absolute RPATHs into the build directory and
//      into whatever conda prefix supplied HDF5. CMake rewrites those to
//      @loader_path / $ORIGIN only on install. A copied binary works on the
//      machine that built it and fails at require() on the user's.
//   2. It VERIFIES the result: every dynamic dependency must resolve either
//      inside the prebuild directory or to an OS library. A prebuild that
//      reaches outside is rejected here, at build time, rather than discovered
//      by the first person to `npm i tttrlib` on a clean machine.
//
// The modules are built STATIC on purpose (-DTTTRLIB_MODULE_TYPE=STATIC): the
// published artefact is then a single .node plus at most a couple of
// third-party libraries, instead of the ~35 sibling module libraries a SHARED
// build produces. Nothing about the API changes -- registries still populate,
// which the smoke test at the end checks by counting registry entries.

import { spawnSync } from 'node:child_process';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import process from 'node:process';
import { fileURLToPath } from 'node:url';

const PKG_DIR = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const REPO_ROOT = path.resolve(PKG_DIR, '..', '..', '..');

// ---------------------------------------------------------------------------
// arguments
// ---------------------------------------------------------------------------
function parseArgs(argv) {
  const opts = {
    build: path.join(REPO_ROOT, 'build-js-prebuild'),
    out: path.join(PKG_DIR, 'prebuilds'),
    jobs: String(os.availableParallelism ? os.availableParallelism() : os.cpus().length),
    configure: true,
    verify: true,
    cmakeArgs: [],
  };
  for (let i = 0; i < argv.length; i++) {
    const a = argv[i];
    if (a === '--build') opts.build = path.resolve(argv[++i]);
    else if (a === '--out') opts.out = path.resolve(argv[++i]);
    else if (a === '--jobs' || a === '-j') opts.jobs = argv[++i];
    else if (a === '--no-configure') opts.configure = false;
    else if (a === '--skip-verify') opts.verify = false;
    else if (a === '--') opts.cmakeArgs.push(...argv.slice(i + 1)), (i = argv.length);
    else throw new Error(`prebuild: unknown argument ${a}`);
  }
  return opts;
}

// ---------------------------------------------------------------------------
// the target triple and the file name node-gyp-build looks for
// ---------------------------------------------------------------------------
// The libc tag is not decoration. A glibc-linked binary loaded on Alpine
// crashes, and an untagged `node.napi.node` matches every libc there is -- so
// the Linux prebuild says which one it is and a musl host gets an honest
// "no native build was found" instead.
function targetTriple() {
  const platform = process.env.npm_config_platform || os.platform();
  const arch = process.env.npm_config_arch || os.arch();
  let libc = process.env.LIBC || '';
  if (!libc && platform === 'linux') {
    libc = fs.existsSync('/etc/alpine-release') ? 'musl' : 'glibc';
  }
  const tags = ['node', 'napi', libc, 'node'].filter(Boolean);
  return { platform, arch, libc, dir: `${platform}-${arch}`, file: tags.join('.') };
}

// ---------------------------------------------------------------------------
// shelling out
// ---------------------------------------------------------------------------
function run(cmd, args, { cwd = REPO_ROOT, quiet = false } = {}) {
  if (!quiet) console.log(`$ ${cmd} ${args.join(' ')}`);
  const r = spawnSync(cmd, args, { cwd, stdio: quiet ? 'pipe' : 'inherit', encoding: 'utf8' });
  if (r.error) throw r.error;
  if (r.status !== 0) {
    if (quiet && r.stderr) process.stderr.write(r.stderr);
    throw new Error(`${cmd} exited with ${r.status}`);
  }
  return r.stdout || '';
}

function tryRun(cmd, args) {
  const r = spawnSync(cmd, args, { encoding: 'utf8' });
  return r.status === 0 ? r.stdout : null;
}

function haveNinja() {
  return spawnSync('ninja', ['--version'], { encoding: 'utf8' }).status === 0;
}

// ---------------------------------------------------------------------------
// verification: nothing may resolve outside the prebuild directory
// ---------------------------------------------------------------------------
// Anything under these prefixes ships with the operating system and is present
// on every machine that can run Node at all. Everything else has to be in the
// prebuild directory or the package is not self-contained.
const SYSTEM_PREFIXES = {
  darwin: ['/usr/lib/', '/System/Library/', '/Library/Apple/'],
  linux: ['/lib/', '/lib64/', '/usr/lib/', '/usr/lib64/', '/usr/lib/x86_64-linux-gnu/',
          '/usr/lib/aarch64-linux-gnu/', 'linux-vdso.so'],
  win32: [],
};

// Windows has no dependency walker in a bare runner image, and the DLLs the
// MSVC toolchain leaves behind are the redistributable ones every Node install
// already has. The smoke test is the check that matters there.
const WINDOWS_SYSTEM_DLLS = /^(kernel32|user32|advapi32|ws2_32|shell32|ole32|oleaut32|msvcp\d+|vcruntime\d+|api-ms-|ext-ms-|ucrtbase|python\d+|bcrypt|crypt32|dbghelp|iphlpapi|psapi|rpcrt4|secur32|shlwapi|userenv|version|winmm|ntdll|node)\.dll$/i;

function isSystemPath(p, platform) {
  return (SYSTEM_PREFIXES[platform] || []).some((prefix) => p.startsWith(prefix));
}

function verifyDarwin(dir, files) {
  const present = new Set(fs.readdirSync(dir));
  const problems = [];
  for (const f of files) {
    const out = tryRun('otool', ['-L', path.join(dir, f)]);
    if (out === null) { problems.push(`${f}: otool failed`); continue; }
    for (const line of out.split('\n').slice(1)) {
      const dep = line.trim().split(' ')[0];
      if (!dep) continue;
      if (dep.startsWith('@rpath/') || dep.startsWith('@loader_path/')) {
        const base = path.basename(dep);
        if (!present.has(base)) problems.push(`${f}: ${dep} is not in the prebuild directory`);
      } else if (dep.startsWith('@executable_path/')) {
        problems.push(`${f}: ${dep} resolves against the host executable`);
      } else if (dep.startsWith('/') && !isSystemPath(dep, 'darwin')) {
        problems.push(`${f}: absolute non-system dependency ${dep}`);
      }
    }
    // An @rpath dependency is only findable if @loader_path is on the rpath.
    const load = tryRun('otool', ['-l', path.join(dir, f)]) || '';
    const rpaths = [...load.matchAll(/cmd LC_RPATH[\s\S]*?path ([^\s]+)/g)].map((m) => m[1]);
    const usesRpath = out.includes('@rpath/');
    if (usesRpath && !rpaths.some((r) => r === '@loader_path' || r.startsWith('@loader_path/'))) {
      problems.push(`${f}: uses @rpath but has no @loader_path RPATH (has: ${rpaths.join(', ') || 'none'})`);
    }
    for (const r of rpaths) {
      if (r.startsWith('/')) problems.push(`${f}: absolute RPATH ${r} leaks the build machine`);
    }
  }
  return problems;
}

function verifyLinux(dir, files) {
  const present = new Set(fs.readdirSync(dir));
  const problems = [];
  for (const f of files) {
    const full = path.join(dir, f);
    // ldd resolves $ORIGIN relative to the object, which is exactly the
    // question being asked. Run it with a clean LD_LIBRARY_PATH so a path that
    // happens to be set on the build machine cannot make a broken prebuild look
    // resolvable.
    const r = spawnSync('ldd', [full], {
      encoding: 'utf8',
      env: { ...process.env, LD_LIBRARY_PATH: '' },
    });
    if (r.status !== 0) { problems.push(`${f}: ldd failed`); continue; }
    for (const line of r.stdout.split('\n')) {
      const t = line.trim();
      if (!t) continue;
      if (t.includes('not found')) { problems.push(`${f}: unresolved ${t}`); continue; }
      const m = /=>\s+(\S+)/.exec(t);
      const resolved = m ? m[1] : t.split(' ')[0];
      if (!resolved.startsWith('/')) continue;   // linux-vdso and friends
      if (path.dirname(path.resolve(resolved)) === path.resolve(dir)) continue;
      if (present.has(path.basename(resolved))) continue;
      if (isSystemPath(resolved, 'linux')) continue;
      problems.push(`${f}: non-system dependency outside the prebuild: ${resolved}`);
    }
    const rp = tryRun('objdump', ['-p', full]) || '';
    for (const m of rp.matchAll(/R(?:UN)?PATH\s+(\S+)/g)) {
      for (const entry of m[1].split(':')) {
        if (entry.startsWith('/')) problems.push(`${f}: absolute RPATH ${entry} leaks the build machine`);
      }
    }
  }
  return problems;
}

function verifyWindows(dir, files) {
  const present = new Set(fs.readdirSync(dir).map((f) => f.toLowerCase()));
  const problems = [];
  for (const f of files) {
    const out = tryRun('dumpbin', ['/dependents', path.join(dir, f)]);
    if (out === null) {
      console.warn(`  ! dumpbin not on PATH; skipping the import check for ${f}`);
      continue;
    }
    for (const m of out.matchAll(/^\s+(\S+\.dll)\s*$/gim)) {
      const dll = m[1];
      if (WINDOWS_SYSTEM_DLLS.test(dll)) continue;
      if (present.has(dll.toLowerCase())) continue;
      problems.push(`${f}: imports ${dll}, which is neither a system DLL nor bundled`);
    }
  }
  return problems;
}

function verify(dir, platform) {
  const files = fs.readdirSync(dir).filter((f) => /\.(node|dylib|so|so\.\d+|dll)$/.test(f) || /\.so\./.test(f));
  const problems = platform === 'darwin' ? verifyDarwin(dir, files)
    : platform === 'linux' ? verifyLinux(dir, files)
      : verifyWindows(dir, files);
  return { files, problems };
}

// ---------------------------------------------------------------------------
// smoke test: load the prebuild the way node-gyp-build will
// ---------------------------------------------------------------------------
// Run in a child process. A dynamic-loader failure in an addon is a process
// abort often enough that catching it in this one is not an option.
function smokeTest() {
  const script = `
    const t = require(${JSON.stringify(PKG_DIR)});
    const reg = t.registry();
    const areas = Object.keys(reg).length;
    if (!areas) throw new Error('registry() is empty -- static initialisers were dropped');
    const entries = Object.values(reg).reduce((n, v) => n + (Array.isArray(v) ? v.length : Object.keys(v).length), 0);
    if (!entries) throw new Error('registry() has areas but no entries');
    if (typeof t.arraysAreZeroCopy !== 'function') throw new Error('arraysAreZeroCopy missing');
    console.log('registry: ' + areas + ' areas, ' + entries + ' entries; zero-copy=' + t.arraysAreZeroCopy());
  `;
  // PREBUILDS_ONLY stops node-gyp-build falling back to a build/Release left in
  // the package directory, so this really does exercise the prebuild.
  const r = spawnSync(process.execPath, ['-e', script], {
    encoding: 'utf8',
    env: { ...process.env, PREBUILDS_ONLY: '1', TTTRLIB_ADDON: '' },
  });
  process.stdout.write(r.stdout || '');
  if (r.status !== 0) {
    process.stderr.write(r.stderr || '');
    throw new Error('prebuild smoke test failed');
  }
}

// ---------------------------------------------------------------------------
// main
// ---------------------------------------------------------------------------
function main() {
  const opts = parseArgs(process.argv.slice(2));
  const target = targetTriple();
  const outDir = path.join(opts.out, target.dir);

  console.log(`tttrlib prebuild: ${target.dir} -> ${path.relative(PKG_DIR, outDir)}/${target.file}`);

  if (opts.configure) {
    run('cmake', [
      '-S', REPO_ROOT,
      '-B', opts.build,
      // Ninja where it exists, for the same reason the rest of CI uses it --
      // and on Windows especially, because the default Visual Studio generator
      // is multi-config: CMAKE_BUILD_TYPE is ignored there and `cmake --build`
      // silently produces Debug. (--config Release below covers that case too,
      // so a machine without ninja still gets an optimised binary.)
      ...(haveNinja() ? ['-G', 'Ninja'] : []),
      '-DCMAKE_BUILD_TYPE=Release',
      '-DBUILD_PYTHON_INTERFACE=OFF',
      '-DBUILD_JAVASCRIPT_INTERFACE=ON',
      '-DBUILD_LIBRARY=OFF',
      // One self-contained .node instead of ~35 sibling module libraries.
      '-DTTTRLIB_MODULE_TYPE=STATIC',
      // A published binary must run on every CPU of its architecture; WITH_AVX
      // would bake in AVX2 and SIGILL on anything older. The runtime dispatch
      // in include/info.h still selects AVX kernels where the CPU has them.
      '-DWITH_AVX=OFF',
      ...opts.cmakeArgs,
    ]);
  }
  // --config Release is a no-op for single-config generators and the difference
  // between an optimised and a debug binary for multi-config ones.
  run('cmake', ['--build', opts.build, '--target', 'tttrlibJs',
    '--config', 'Release', '--parallel', opts.jobs]);

  fs.rmSync(outDir, { recursive: true, force: true });
  run('cmake', ['--install', opts.build, '--component', 'js',
    '--config', 'Release', '--prefix', outDir]);

  // node-gyp-build picks by file name, not by directory contents. CMake installs
  // the MODULE target under its OUTPUT_NAME on every platform, so the rename is
  // the same one everywhere.
  const built = path.join(outDir, 'tttrlib.node');
  if (!fs.existsSync(built)) {
    throw new Error(`cmake --install produced no ${built}\ncontents: ${fs.readdirSync(outDir).join(', ')}`);
  }
  fs.renameSync(built, path.join(outDir, target.file));

  console.log(`\nstaged ${target.dir}:`);
  for (const f of fs.readdirSync(outDir).sort()) {
    console.log(`  ${f}  (${(fs.statSync(path.join(outDir, f)).size / 1048576).toFixed(1)} MB)`);
  }

  if (opts.verify) {
    const { problems } = verify(outDir, target.platform);
    if (problems.length) {
      console.error('\nprebuild is not self-contained:');
      for (const p of problems) console.error(`  - ${p}`);
      console.error(
        '\nEvery dependency must resolve inside the prebuild directory or to an OS\n' +
        'library. Re-run with --skip-verify only to inspect the failure, never to\n' +
        'publish the result.');
      process.exit(1);
    }
    console.log('self-contained: every dependency resolves inside the prebuild or to an OS library');
  }

  smokeTest();
  console.log(`\nprebuild ok: ${path.join(outDir, target.file)}`);
}

main();
