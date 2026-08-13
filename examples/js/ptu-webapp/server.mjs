// SPDX-License-Identifier: BSD-3-Clause
//
// A minimal web application over the tttrlib JavaScript binding: open a PTU (or
// any other TTTR file), and see its header, its time trace and its micro-time
// decay in a browser.
//
// It is deliberately small -- one file of server, one page of front end, no
// framework, no build step, no dependencies beyond Node and tttrlib itself -- so
// it can be read start to finish. It is the shop window for the binding and the
// starting point for the burst and single-molecule applications, which
// is why the two things those need are already done properly here:
//
//   * every tttrlib call runs in a worker_thread, so a 200 MB read does not
//     stall the server (a synchronous read on the main thread would);
//   * the browser never receives photon arrays. Binning and histogramming happen
//     in C++, and a few thousand points cross the wire instead of a few hundred
//     million.
//
// LOCALHOST ONLY. It binds to 127.0.0.1 and serves files from one configured
// data root, with containment checked server-side. It has no authentication and
// is not written for exposure to a network.

import http from 'node:http';
import path from 'node:path';
import fs from 'node:fs/promises';
import { fileURLToPath } from 'node:url';
import { Worker } from 'node:worker_threads';

const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, '..', '..', '..');

const PORT = Number(process.env.PORT || 8030);
// Where files may be read from. Everything the client asks for is resolved
// against this and rejected if it escapes -- a path parameter that can read any
// file on the host is the obvious hole in an application shaped like this one.
const DATA_ROOT = path.resolve(
  process.env.TTTRLIB_DATA || path.join(REPO_ROOT, 'tttr-data'));

// ---------------------------------------------------------------------------
// Worker pool
// ---------------------------------------------------------------------------
// One worker per request, capped. tttrlib's calls are synchronous and CPU-bound;
// running them on the main thread would block every other request for the whole
// duration of a read.
const MAX_WORKERS = 4;
let running = 0;
const queue = [];

function runInWorker(message) {
  return new Promise((resolve, reject) => {
    const start = () => {
      running++;
      const worker = new Worker(path.join(HERE, 'worker.mjs'), { workerData: message });
      let settled = false;
      const done = (fn, arg) => {
        if (settled) return;
        settled = true;
        running--;
        const next = queue.shift();
        if (next) next();
        fn(arg);
      };
      worker.once('message', (m) => {
        worker.terminate();
        m.error ? done(reject, new Error(m.error)) : done(resolve, m.result);
      });
      worker.once('error', (e) => done(reject, e));
      worker.once('exit', (code) => {
        if (code !== 0) done(reject, new Error(`worker exited with code ${code}`));
      });
    };
    running < MAX_WORKERS ? start() : queue.push(start);
  });
}

// ---------------------------------------------------------------------------
// Path containment
// ---------------------------------------------------------------------------
/** Resolve a client-supplied relative path inside DATA_ROOT, or throw. */
function resolveInRoot(relative) {
  const abs = path.resolve(DATA_ROOT, relative);
  // path.relative() rather than a string prefix test: "/data-evil" starts with
  // "/data" as a string but is not inside it.
  const rel = path.relative(DATA_ROOT, abs);
  if (rel.startsWith('..') || path.isAbsolute(rel)) {
    throw new Error('path escapes the data root');
  }
  return abs;
}

// ---------------------------------------------------------------------------
// Routes
// ---------------------------------------------------------------------------
const TTTR_EXTENSIONS = new Set(['.ptu', '.ht3', '.spc', '.pt3', '.t3r', '.hdf5', '.h5', '.sm', '.photons']);

/** Every readable TTTR file under the data root, relative to it. */
async function listFiles(dir = DATA_ROOT, prefix = '', out = []) {
  let entries;
  try {
    entries = await fs.readdir(dir, { withFileTypes: true });
  } catch {
    return out;
  }
  for (const e of entries) {
    const rel = prefix ? `${prefix}/${e.name}` : e.name;
    // Symlinks are not followed: they are the other way out of the data root.
    if (e.isDirectory()) await listFiles(path.join(dir, e.name), rel, out);
    else if (e.isFile() && TTTR_EXTENSIONS.has(path.extname(e.name).toLowerCase())) {
      out.push(rel);
    }
  }
  return out;
}

function json(res, status, body) {
  const payload = JSON.stringify(body);
  res.writeHead(status, {
    'content-type': 'application/json; charset=utf-8',
    'content-length': Buffer.byteLength(payload),
  });
  res.end(payload);
}

const STATIC = {
  '/': ['index.html', 'text/html; charset=utf-8'],
  '/index.html': ['index.html', 'text/html; charset=utf-8'],
  '/app.js': ['app.js', 'text/javascript; charset=utf-8'],
  '/style.css': ['style.css', 'text/css; charset=utf-8'],
};

const server = http.createServer(async (req, res) => {
  const url = new URL(req.url, 'http://localhost');

  try {
    if (STATIC[url.pathname]) {
      const [file, type] = STATIC[url.pathname];
      const body = await fs.readFile(path.join(HERE, 'web', file));
      res.writeHead(200, { 'content-type': type, 'content-length': body.length });
      return res.end(body);
    }

    if (url.pathname === '/api/files') {
      return json(res, 200, { root: DATA_ROOT, files: await listFiles() });
    }

    // The registry, verbatim. The front end's only source of knowledge about
    // what this build of tttrlib can do.
    if (url.pathname === '/api/registry') {
      return json(res, 200, await runInWorker({ op: 'registry' }));
    }

    if (url.pathname === '/api/open') {
      const rel = url.searchParams.get('path');
      if (!rel) return json(res, 400, { error: 'missing ?path=' });
      const bins = Math.min(Number(url.searchParams.get('bins') || 2000), 20000);
      const result = await runInWorker({
        op: 'open',
        filename: resolveInRoot(rel),
        bins,
      });
      return json(res, 200, result);
    }

    json(res, 404, { error: 'not found' });
  } catch (err) {
    json(res, 400, { error: String(err && err.message || err) });
  }
});

server.listen(PORT, '127.0.0.1', () => {
  console.log(`tttrlib PTU viewer on http://127.0.0.1:${PORT}`);
  console.log(`serving TTTR files from ${DATA_ROOT}`);
});
