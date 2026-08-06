// SPDX-License-Identifier: BSD-3-Clause
//
// The front end. Plain ES modules and <canvas>: no framework and no build step,
// so this file is readable as source and cannot rot through a toolchain upgrade.
//
// It does no analysis. Everything it draws was computed in C++ and arrived as a
// few thousand numbers; the browser's job is to put them on a screen.

const $ = (id) => document.getElementById(id);

const PALETTE = ['#2f6fdb', '#d9793a', '#3f9e6b', '#a4519e', '#c0392b', '#8a7d3f'];

// ---------------------------------------------------------------------------
// Plotting
// ---------------------------------------------------------------------------
// Canvas is sized in CSS pixels but drawn at device resolution, or every line
// is soft on a retina display.
function prepare(canvas) {
  const dpr = window.devicePixelRatio || 1;
  const cssWidth = canvas.clientWidth || canvas.width;
  const cssHeight = Number(canvas.getAttribute('height'));
  canvas.width = Math.round(cssWidth * dpr);
  canvas.height = Math.round(cssHeight * dpr);
  const ctx = canvas.getContext('2d');
  ctx.setTransform(dpr, 0, 0, dpr, 0, 0);
  ctx.clearRect(0, 0, cssWidth, cssHeight);
  return { ctx, w: cssWidth, h: cssHeight };
}

const css = (name) => getComputedStyle(document.documentElement).getPropertyValue(name).trim();

function axes(ctx, w, h, pad, xLabel, yLabel) {
  ctx.strokeStyle = css('--line');
  ctx.lineWidth = 1;
  ctx.beginPath();
  ctx.moveTo(pad.l, pad.t);
  ctx.lineTo(pad.l, h - pad.b);
  ctx.lineTo(w - pad.r, h - pad.b);
  ctx.stroke();

  ctx.fillStyle = css('--muted');
  ctx.font = '11px system-ui, sans-serif';
  ctx.textAlign = 'center';
  ctx.fillText(xLabel, (pad.l + w - pad.r) / 2, h - 4);
  ctx.save();
  ctx.translate(11, (pad.t + h - pad.b) / 2);
  ctx.rotate(-Math.PI / 2);
  ctx.fillText(yLabel, 0, 0);
  ctx.restore();
}

/** Multi-series line plot on a linear y axis. */
function plotLines(canvas, series, xMax, xLabel, yLabel) {
  const { ctx, w, h } = prepare(canvas);
  const pad = { l: 46, r: 10, t: 10, b: 26 };
  const yMax = Math.max(1, ...series.map((s) => Math.max(...s.values)));

  axes(ctx, w, h, pad, xLabel, yLabel);

  const px = (i, n) => pad.l + ((w - pad.l - pad.r) * i) / Math.max(1, n - 1);
  const py = (v) => h - pad.b - ((h - pad.t - pad.b) * v) / yMax;

  series.forEach((s, k) => {
    ctx.strokeStyle = s.color || PALETTE[k % PALETTE.length];
    ctx.lineWidth = 1.25;
    ctx.beginPath();
    s.values.forEach((v, i) => (i ? ctx.lineTo(px(i, s.values.length), py(v))
                                 : ctx.moveTo(px(i, s.values.length), py(v))));
    ctx.stroke();
  });

  ctx.fillStyle = css('--muted');
  ctx.textAlign = 'right';
  ctx.fillText(String(Math.round(yMax)), pad.l - 5, pad.t + 9);
  ctx.fillText('0', pad.l - 5, h - pad.b);
  ctx.textAlign = 'left';
  ctx.fillText('0', pad.l, h - pad.b + 13);
  ctx.textAlign = 'right';
  ctx.fillText(xMax, w - pad.r, h - pad.b + 13);
}

/** Single series on a log y axis -- a fluorescence decay spans decades. */
function plotLog(canvas, values, xMax, xLabel, yLabel) {
  const { ctx, w, h } = prepare(canvas);
  const pad = { l: 46, r: 10, t: 10, b: 26 };
  const yMax = Math.max(1, ...values);
  const top = Math.log10(yMax);

  axes(ctx, w, h, pad, xLabel, yLabel);

  const px = (i) => pad.l + ((w - pad.l - pad.r) * i) / Math.max(1, values.length - 1);
  // Empty bins have no place on a log axis; drop to the baseline instead of -Inf.
  const py = (v) => (v <= 0
    ? h - pad.b
    : h - pad.b - ((h - pad.t - pad.b) * Math.log10(v)) / (top || 1));

  ctx.strokeStyle = PALETTE[0];
  ctx.lineWidth = 1.25;
  ctx.beginPath();
  values.forEach((v, i) => (i ? ctx.lineTo(px(i), py(v)) : ctx.moveTo(px(i), py(v))));
  ctx.stroke();

  ctx.fillStyle = css('--muted');
  ctx.textAlign = 'right';
  ctx.fillText(String(Math.round(yMax)), pad.l - 5, pad.t + 9);
  ctx.fillText('1', pad.l - 5, h - pad.b);
  ctx.textAlign = 'left';
  ctx.fillText('0', pad.l, h - pad.b + 13);
  ctx.textAlign = 'right';
  ctx.fillText(xMax, w - pad.r, h - pad.b + 13);
}

// ---------------------------------------------------------------------------
// Formatting
// ---------------------------------------------------------------------------
const int = (n) => n.toLocaleString('en-US');

/** Seconds as a value a person reads: 3.3 ps, 25 ns, 1.4 s. */
function seconds(v) {
  if (!isFinite(v) || v === 0) return String(v);
  const units = [[1, 's'], [1e-3, 'ms'], [1e-6, 'µs'], [1e-9, 'ns'], [1e-12, 'ps']];
  for (const [scale, name] of units) {
    if (v >= scale) return `${(v / scale).toPrecision(4)} ${name}`;
  }
  return v.toExponential(3) + ' s';
}

function summary(d) {
  const dl = $('summary');
  dl.textContent = '';
  const rows = [
    ['Events', int(d.nEvents)],
    ['Container', d.containerType ?? '—'],
    ['Routing channels', d.usedChannels.join(', ') || '—'],
    ['Acquisition time', seconds(d.acquisitionTime)],
    ['Macro-time resolution', seconds(d.macroTimeResolution)],
    ['Micro-time resolution', seconds(d.microTimeResolution)],
    ['Micro-time channels', int(d.nMicroTimeChannels)],
    ['Header tags', int(d.nHeaderTags ?? 0)],
    ['Array mode', d.zeroCopy ? 'zero-copy views' : 'copied'],
  ];
  for (const [k, v] of rows) {
    const dt = document.createElement('dt');
    dt.textContent = k;
    const dd = document.createElement('dd');
    dd.textContent = v;
    dl.append(dt, dd);
  }
}

function legend(channels) {
  const el = $('legend');
  el.textContent = '';
  channels.forEach((c, k) => {
    const span = document.createElement('span');
    const swatch = document.createElement('i');
    swatch.style.background = PALETTE[k % PALETTE.length];
    span.append(swatch, document.createTextNode(`channel ${c}`));
    el.append(span);
  });
}

// ---------------------------------------------------------------------------
// Wiring
// ---------------------------------------------------------------------------
function setStatus(text, isError = false) {
  const el = $('status');
  el.textContent = text;
  el.classList.toggle('error', isError);
}

let current = null;

async function loadFileList() {
  const r = await fetch('/api/files');
  const { root, files } = await r.json();
  $('root').textContent = `${files.length} file(s) under ${root}`;
  const sel = $('file');
  sel.textContent = '';
  if (!files.length) {
    sel.append(new Option('no TTTR files found', ''));
    setStatus('No TTTR files under the data root. Run test/download_test_data.py, ' +
              'or set TTTRLIB_DATA and restart the server.');
    return;
  }
  for (const f of files) sel.append(new Option(f, f));
  $('open').disabled = false;
}

async function open() {
  const rel = $('file').value;
  if (!rel) return;
  $('open').disabled = true;
  setStatus(`Reading ${rel}…`);
  try {
    const r = await fetch(`/api/open?path=${encodeURIComponent(rel)}&bins=2000`);
    const d = await r.json();
    if (!r.ok) throw new Error(d.error || r.statusText);
    current = d;
    render(d);
    setStatus(`${int(d.nEvents)} events.`);
  } catch (err) {
    setStatus(String(err.message || err), true);
  } finally {
    $('open').disabled = false;
  }
}

function render(d) {
  $('results').hidden = false;
  summary(d);

  const nBins = d.trace.bins[0]?.length ?? 0;
  const totalSeconds = d.trace.binSeconds * nBins;
  plotLines(
    $('trace'),
    d.trace.bins.map((values, k) => ({ values, color: PALETTE[k % PALETTE.length] })),
    seconds(totalSeconds),
    'time',
    `counts / ${seconds(d.trace.binSeconds)}`);
  legend(d.trace.channels);

  const lastTime = d.decay.time[d.decay.time.length - 1] ?? 0;
  plotLog($('decay'), d.decay.counts, seconds(lastTime), 'micro time', 'counts (log)');

  $('header').textContent = JSON.stringify(d.header, null, 2);
}

$('open').addEventListener('click', open);
// Redraw on resize so the canvas stays sharp and correctly proportioned.
window.addEventListener('resize', () => { if (current) render(current); });

loadFileList().catch((e) => setStatus(String(e.message || e), true));
