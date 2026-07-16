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
