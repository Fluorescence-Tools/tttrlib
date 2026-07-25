// SPDX-License-Identifier: BSD-3-Clause
// SWIG bindings for the additive photon-simulation subsystem (PRD-005).
// All classes are Sim-prefixed and live in namespace tttrlib.

%{
#include "SimRandom.h"
#include "SimDecay.h"
#include "SimSpecies.h"
#include "SimGrid.h"
#include "SimSystem.h"
#include "SimScanner.h"
#include "SimIntegrator.h"
#include "SimMicrotimeEncoder.h"
#include "SimEngine.h"
%}

%include "stdint.i"

// Extra std::vector instantiations not already provided by misc_types.i.
%template(VectorUint8)  std::vector<unsigned char>;
%template(VectorUint16) std::vector<unsigned short>;
%template(VectorInt8)   std::vector<signed char>;

// Hide raw-pointer overloads; Python uses the vector/convenience forms instead.
%ignore tttrlib::SimMicrotimeEncoder::encode;                 // use SimEngine.encode(...)
%ignore tttrlib::SimSystem::set_positions;                    // raw-pointer form
%ignore tttrlib::SimSystem::set_emitter_grid(const int*, int, int, int, int,
                                             double, double, double, double, double, double);

%include "SimRandom.h"

%include "SimDecay.h"
%template(VectorSimDecay) std::vector<tttrlib::SimDecay>;

%include "SimSpecies.h"
%template(VectorSimSpecies) std::vector<tttrlib::SimSpecies>;

// Thin numpy PSF builders (convenience only; wrap the C++ fillers). PRD-008.
%extend tttrlib::SimGrid {
    %pythoncode %{
        @staticmethod
        def numeric_from_numpy(rz, r_step, z_step, extent_xy=2.0, extent_z=4.0,
                               spacing=0.1, amplitude=1.0):
            """Build a SimGrid from a numeric/measured radially-symmetric PSF.

            ``rz`` is a 2D array ``[nz, nr]`` sampled on the ``(r, z)`` half-plane
            (``r >= 0``, ``z`` centred); each 3D voxel is filled by bilinear
            interpolation at ``(sqrt(x**2+y**2), z)`` (PyBroMo NumericPSF convention).
            """
            import numpy as _np
            a = _np.ascontiguousarray(rz, dtype=float)
            nz, nr = a.shape
            return SimGrid.from_radial(a.ravel().tolist(), int(nr), int(nz),
                                       float(r_step), float(z_step),
                                       extent_xy, extent_z, spacing, amplitude)

        @staticmethod
        def numeric_from_file(path, r_step=None, z_step=None, extent_xy=2.0,
                              extent_z=4.0, spacing=0.1, amplitude=1.0):
            """Load a radial PSF from ``.npy`` (2D ``[nz,nr]``), ``.npz`` (``rz`` + steps)
            or ``.mat`` (PyBroMo) and build a SimGrid. ``r_step``/``z_step`` in µm."""
            import numpy as _np, os
            ext = os.path.splitext(path)[1].lower()
            if ext == ".npy":
                rz = _np.load(path)
            elif ext == ".npz":
                d = _np.load(path)
                rz = d["rz"] if "rz" in d.files else d[d.files[0]]
                if r_step is None and "r_step" in d.files: r_step = float(d["r_step"])
                if z_step is None and "z_step" in d.files: z_step = float(d["z_step"])
            elif ext == ".mat":
                from scipy.io import loadmat
                m = loadmat(path)
                key = next(k for k in m if not k.startswith("__"))
                rz = _np.asarray(m[key], dtype=float)
            else:
                raise ValueError("unsupported PSF file (use .npy/.npz/.mat): " + path)
            if r_step is None or z_step is None:
                raise ValueError("r_step and z_step (µm) are required for this file")
            return SimGrid.numeric_from_numpy(rz, r_step, z_step, extent_xy,
                                              extent_z, spacing, amplitude)
    %}
}

%include "SimGrid.h"
%template(VectorSimGrid) std::vector<tttrlib::SimGrid>;

%include "SimSystem.h"
%include "SimScanner.h"
%include "SimIntegrator.h"
%include "SimMicrotimeEncoder.h"

// Thin ergonomic layer on the driver (convenience only). PRD-008.
%extend tttrlib::SimEngine {
    %pythoncode %{
        @staticmethod
        def from_dict(config):
            """Build a SimEngine from a config dict — the JSON single entry point.

            ``config`` follows ``examples/simulation/sim.schema.json``. Equivalent to
            ``SimEngine.from_json(json.dumps(config))``.
            """
            import json as _json
            return SimEngine.from_json(_json.dumps(config))

        def photons(self):
            """The photon stream as a dict of numpy arrays (no VectorDouble wrapping):

            ``macro_window`` (window index), ``arrival_time`` (within-window, macro units),
            ``channel`` (routing/detector), ``micro_time`` (TAC channel index),
            ``species`` (== n_species marks background), ``molecule`` (emitter id),
            ``event_type`` (0 = photon, 1 = marker).
            """
            import numpy as _np
            return dict(
                macro_window=_np.asarray(self.macro_window()),
                arrival_time=_np.asarray(self.arrival_time()),
                channel=_np.asarray(self.channel()),
                micro_time=_np.asarray(self.micro_time()),
                species=_np.asarray(self.emitting_species()),
                molecule=_np.asarray(self.emitting_molecule()),
                event_type=_np.asarray(self.event_type()),
            )

        def state_trajectory(self):
            """The state trajectory as a dict of numpy arrays (see ``set_state_log``).

            One row per recorded event, time-ordered: ``window`` and ``time``
            (within-window, macro units) locate it, ``molecule`` says which emitter,
            and ``from``/``to`` are the states it left and entered. ``from == -1``
            marks a birth (the molecule's initial state) and ``to == -1`` a death
            (it left the box), so the log is self-contained.

            ``macro_time`` is added for convenience: the absolute time
            ``window * dt + time`` in macro-time units.

            Rows are sorted by time. The C++ arrays are in recording order, which differs
            when a coasting molecule is caught up on waking and its transitions are appended
            after later events of other molecules.
            """
            import numpy as _np
            w = _np.asarray(self.state_window())
            t = _np.asarray(self.state_time())
            mt = w * self.settings().dt + t
            mol = _np.asarray(self.state_molecule())
            order = _np.lexsort((mol, mt))
            return dict(
                window=w[order], time=t[order],
                macro_time=mt[order], molecule=mol[order],
                **{"from": _np.asarray(self.state_from())[order],
                   "to": _np.asarray(self.state_to())[order]},
            )

        def state_occupancy(self, windows_per_bin=1, window_start=0, window_stop=None):
            """Fraction of each time bin that each molecule spent in each state.

            Reduces the state trajectory (``set_state_log``) to the quantity most
            analyses actually want: a time-average of a state-dependent observable
            is an average over these fractions. Because the log is event-based the
            fractions are exact — a state entered and left inside one bin still
            contributes its true dwell time, which a strided position/state
            snapshot cannot represent.

            Bins are ``windows_per_bin`` macro-windows wide, covering
            ``[window_start, window_stop)``; ``window_stop`` defaults to the end of the
            simulation, not to the last recorded transition — a molecule that stops
            exchanging still occupies its final state for the rest of the run.
            Returns ``(molecules, fractions)`` with ``molecules`` the sorted
            molecule ids and ``fractions`` of shape
            ``(n_molecules, n_bins, n_species)``. Rows for a molecule that is not
            alive anywhere in a bin are all zero, so ``fractions.sum(axis=2)`` also
            tells you what fraction of each bin it was present for.
            """
            import numpy as _np
            tr = self.state_trajectory()
            dt = float(self.settings().dt)
            nsp = int(self.system().n_species())
            per_bin = int(windows_per_bin)
            if per_bin < 1:
                raise ValueError("windows_per_bin must be >= 1")
            mol = tr["molecule"]
            if mol.size == 0:
                raise ValueError(
                    "no state events recorded — call set_state_log(True) before run()")
            time = tr["macro_time"]
            to = tr["to"]

            w0 = int(window_start)
            w1 = int(self.current_window() if window_stop is None else window_stop)
            n_bins = max(0, -(-(w1 - w0) // per_bin))          # ceil
            bin_len = per_bin * dt
            t0 = w0 * dt

            molecules = _np.unique(mol)
            n_mol = molecules.size
            if n_bins == 0:
                return molecules, _np.zeros((n_mol, 0, nsp))
            t_end = t0 + n_bins * bin_len

            # Consecutive events of one molecule bracket an interval spent in one state.
            order = _np.lexsort((time, mol))
            mol, time, to = mol[order], time[order], to[order]
            row = _np.searchsorted(molecules, mol)
            stops = _np.searchsorted(mol, molecules, side="right")

            nxt = _np.empty_like(time)
            nxt[:-1] = time[1:]
            nxt[stops - 1] = t_end            # each molecule's last state runs to the end
            lo = _np.clip(time, t0, t_end)
            hi = _np.clip(nxt, t0, t_end)

            keep = (to >= 0) & (hi > lo)      # a death ends the previous interval, adds none
            row, lo, hi = row[keep], lo[keep], hi[keep]
            st = to[keep].astype(_np.intp)
            i0 = ((lo - t0) // bin_len).astype(_np.intp)
            i1 = _np.minimum(n_bins - 1, ((hi - t0) // bin_len).astype(_np.intp))

            # Split each interval into a head bin, whole interior bins and a tail bin. The
            # interior is accumulated as a difference array so a long dwell costs O(1) rather
            # than one write per bin it spans.
            size = n_mol * n_bins * nsp
            flat = (row * n_bins + i0) * nsp + st
            one = i0 == i1
            span = ~one
            n_span = int(span.sum())
            full = _np.full(n_span, bin_len)
            # bincount returns an integer array when it is handed no weights to sum, so
            # accumulate into an explicitly-typed buffer rather than adopting its dtype.
            acc = _np.zeros(size)
            acc += _np.bincount(flat[one], (hi - lo)[one], minlength=size)
            acc += _np.bincount(flat[span],
                                t0 + (i0[span] + 1) * bin_len - lo[span], minlength=size)
            acc += _np.bincount((row[span] * n_bins + i1[span]) * nsp + st[span],
                                hi[span] - (t0 + i1[span] * bin_len), minlength=size)

            dsize = n_mol * (n_bins + 1) * nsp
            d = _np.zeros(dsize)
            d += _np.bincount((row[span] * (n_bins + 1) + i0[span] + 1) * nsp + st[span],
                              full, minlength=dsize)
            d -= _np.bincount((row[span] * (n_bins + 1) + i1[span]) * nsp + st[span],
                              full, minlength=dsize)
            interior = _np.cumsum(d.reshape(n_mol, n_bins + 1, nsp), axis=1)[:, :n_bins, :]

            return molecules, (acc.reshape(n_mol, n_bins, nsp) + interior) / bin_len

        def state_numpy(self):
            """``get_state()`` as a dict of numpy arrays (window, n_photons, id, species, x, y, z)."""
            import numpy as _np
            s = self.get_state()
            return dict(window=int(s.window), n_photons=int(s.n_photons),
                        id=_np.asarray(s.id), species=_np.asarray(s.species),
                        x=_np.asarray(s.x), y=_np.asarray(s.y), z=_np.asarray(s.z))

        def to_tttr(self, dt, n_channels, ch_conversion=None, n_microtime_channels=4096,
                    microtime_resolution=0.004069, laser_period=13.596, pulsed=False,
                    seed=1, container="SPC-130", reverse_tac=True):
            """Encode the photon stream and return it as a ``tttrlib.TTTR`` (PRD-007 G7).

            One-call export: builds a Becker&Hickl SPC-132 record stream with a
            ``SimMicrotimeEncoder`` and reads it back as a ``TTTR``. ``dt``/``n_channels``
            must match the simulation. ``ch_conversion`` maps a routing channel to a hardware
            channel (default: the 6-detector B&H map). ``pulsed=True`` uses the pulsed TAC path.

            ``reverse_tac=True`` (default) writes the TAC in B&H *reverse start-stop* order
            (raw ADC = ``n_microtime_channels - 1 - micro_time``); the SPC reader un-reverses
            it, so the read-back ``micro_time`` matches the simulation. Set ``reverse_tac=False``
            only to emit the physical micro-time directly (e.g. for external tools that do not
            reverse) — a ``to_tttr`` round-trip is then inverted on read-back.
            """
            import tempfile, os
            enc = SimMicrotimeEncoder()
            enc.n_channels = int(n_channels)
            enc.tw = float(dt)
            enc.pulsed_exc = 1 if pulsed else 0
            enc.n_microtime_channels = int(n_microtime_channels)
            enc.microtime_resolution = float(microtime_resolution)
            enc.laser_period = float(laser_period)
            enc.reverse_tac = bool(reverse_tac)
            if ch_conversion is None:
                # sim channel i -> hardware routing channel i, so the read-back TTTR's
                # routing_channels equal the simulation channels (0..n_channels-1).
                ch_conversion = list(range(int(n_channels)))
            enc.ch_conversion = VectorUint16([int(v) for v in ch_conversion])
            # The encoder ticks macro-time in units of the laser period (SYNC_DT); keep the SPC
            # header's macro-time clock (units of 0.1 ns) consistent, so the read-back absolute
            # time matches the simulation: macro_time_resolution = macro_time_clock * 0.1 ns.
            enc.macro_time_clock = int(round(laser_period * 10.0))
            rec = self.encode(enc, SimRandom(int(seed)))
            fd, path = tempfile.mkstemp(suffix=".spc")
            os.close(fd)
            try:
                with open(path, "wb") as f:
                    f.write(bytes(bytearray(enc.file_bytes(rec.bytes))))
                return TTTR(path, container)
            finally:
                os.remove(path)
    %}
}

%newobject tttrlib::SimEngine::from_json;   // Python owns the returned engine

// Translate C++ exceptions from the engine (e.g. from_json / constructor config validation such
// as a q_alex row-count mismatch) into Python exceptions instead of terminating the interpreter.
%exception {
    try {
        $action
    } catch (const std::invalid_argument& e) {
        SWIG_exception(SWIG_ValueError, e.what());
    } catch (const std::exception& e) {
        SWIG_exception(SWIG_RuntimeError, e.what());
    } catch (...) {
        SWIG_exception(SWIG_UnknownError, "Unknown exception");
    }
}
%include "SimEngine.h"
%exception;   // reset to the previous global handler
