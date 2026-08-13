// SPDX-License-Identifier: BSD-3-Clause
//
// Every tttrlib call in this application happens here, on a worker thread.
//
// The reason is structural rather than stylistic. tttrlib's API is synchronous:
// `new TTTR(path)` reads and decodes the whole file before it returns, which for
// a 200 MB PTU is seconds. On Node's main thread that blocks the event loop --
// no other request is served, not even the one that would report progress. This
// is the operational risk of a synchronous binding, and running the analysis in a worker is
// its mitigation.
//
// The thread boundary is also where the data shrinks. Photon arrays stay here;
// what crosses back is a few thousand binned points and a decay histogram, so
// the main thread and the browser never see a hundred-million-element array.

import { parentPort, workerData } from 'node:worker_threads';
import { createRequire } from 'node:module';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const require = createRequire(import.meta.url);
const HERE = path.dirname(fileURLToPath(import.meta.url));
const REPO_ROOT = path.resolve(HERE, '..', '..', '..');

const tttrlib = require(
  process.env.TTTRLIB_JS_PKG || path.join(REPO_ROOT, 'ext', 'js', 'pkg', 'index.js'));

/**
 * Bin the macro-time stream into `nBins` equal intervals, per routing channel.
 *
 * Done here rather than in the browser because the input is one number per
 * photon and the output is a few thousand -- the whole point of computing on
 * this side of the wire. Macro times are BigInt (they exceed 2^53), so the bin
 * index is computed in BigInt and only the counts become numbers.
 */
function timeTrace(tttr, nBins) {
  const mt = tttr.getMacroTimes();
  const ch = tttr.getRoutingChannel();
  const channels = Array.from(tttr.getUsedRoutingChannels()).map(Number).sort((a, b) => a - b);
  const indexOf = new Map(channels.map((c, i) => [c, i]));

  if (mt.length === 0) return { channels, bins: [], binSeconds: 0 };

  const t0 = mt[0];
  const span = mt[mt.length - 1] - t0;
  const traces = channels.map(() => new Float64Array(nBins));
  // A routing channel seen in the stream but absent from used_routing_channels
  // would index `traces` with undefined and throw. It should not happen, but a
  // viewer that dies on an odd file is worse than one that drops a stray event.
  if (span <= 0n) {
    for (let i = 0; i < mt.length; i++) {
      const k = indexOf.get(ch[i]);
      if (k !== undefined) traces[k][0]++;
    }
  } else {
    const n = BigInt(nBins);
    for (let i = 0; i < mt.length; i++) {
      const k = indexOf.get(ch[i]);
      if (k === undefined) continue;
      // Integer arithmetic throughout: (t - t0) * nBins / span never rounds,
      // which a Number-based version would once macro times pass 2**53.
      let b = Number(((mt[i] - t0) * n) / span);
      if (b >= nBins) b = nBins - 1;         // the last photon lands exactly on the edge
      traces[k][b]++;
    }
  }

  const macroRes = tttr.getHeader().macroTimeResolution;
  return {
    channels,
    binSeconds: (Number(span) * macroRes) / nBins,
    bins: traces.map((t) => Array.from(t)),
  };
}

// container_type is an integer in the header. The registry knows what each one
// is called, so the name comes from there rather than from a table in this file
// that would go stale the moment tttrlib gains a reader.
let _containerNames = null;
function containerName(id) {
  if (_containerNames === null) {
    _containerNames = new Map(
      Object.values(tttrlib.registry('file_container'))
        .map((e) => [e.container_type, e.label || e.name]));
  }
  return _containerNames.get(id) ?? (id === undefined ? null : `type ${id}`);
}

function open(filename, nBins) {
  // The container type is inferred from the file's content, not its extension:
  // the same sniffing the C++ reader does, so the UI never has to guess.
  const tttr = new tttrlib.TTTR(filename);
  const header = tttr.getHeader();
  const headerJson = JSON.parse(header.getJson());

  const [decay, decayTime] = tttr.getMicrotimeHistogram(1);

  return {
    filename,
    nEvents: tttr.size(),
    // %attribute turns the C++ getters into properties, exactly as in Python.
    containerType: containerName(headerJson.MeasDesc_ContainerType ?? header.tttrContainerType),
    macroTimeResolution: header.macroTimeResolution,
    microTimeResolution: header.microTimeResolution,
    nMicroTimeChannels: tttr.getNumberOfMicroTimeChannels(),
    acquisitionTime: tttr.acquisitionTime,
    usedChannels: Array.from(tttr.getUsedRoutingChannels()).map(Number),
    // The full header, so the page can show every tag the file carries without
    // this file deciding in advance which ones matter.
    header: headerJson,
    nHeaderTags: Array.isArray(headerJson.tags) ? headerJson.tags.length : 0,
    trace: timeTrace(tttr, nBins),
    decay: {
      counts: Array.from(decay),
      // Seconds, from the C++ axis -- not recomputed here, so it cannot drift
      // from what the decay fits would use.
      time: Array.from(decayTime),
    },
    zeroCopy: tttrlib.arraysAreZeroCopy(),
  };
}

try {
  let result;
  switch (workerData.op) {
    case 'registry':
      result = tttrlib.registry();
      break;
    case 'open':
      result = open(workerData.filename, workerData.bins);
      break;
    default:
      throw new Error(`unknown op '${workerData.op}'`);
  }
  parentPort.postMessage({ result });
} catch (err) {
  parentPort.postMessage({ error: String((err && err.message) || err) });
}
