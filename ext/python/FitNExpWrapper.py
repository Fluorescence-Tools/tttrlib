"""Instance-based multi-exponential Poisson reconvolution fitter.

Wraps the static :class:`DecayFitNExp` C++ API in a stateful object that
holds the instrument description (IRF, dt, period, bounds) and provides
single-curve, batch, and per-pixel image fitting.
"""


class FitNExp:
    """Bounded multi-exponential reconvolution by Poisson maximum likelihood.

    The IRF, timing, and lifetime bounds are set once at construction.
    Each call to :meth:`__call__`, :meth:`fit_many`, or :meth:`fit_map`
    passes data and initial lifetimes; the fitter profiles amplitudes by
    EM and optimises free lifetimes by deterministic coordinate-wise
    Brent minimisation.

    Args:
        dt: Time difference between micro-time channels (ns).
        irf: 1-D instrument response function.
        period: Excitation period (ns). Non-positive selects n_bins * dt.
        convolution_stop: Last channel used in the IRF reconvolution.
        tau_min: Lower bound on fitted lifetimes.
        tau_max: Upper bound on fitted lifetimes.
        tail_start: If >= 0, switch to tail-fit mode starting at this channel.
        max_outer_iterations: Max coordinate-sweep rounds.
        max_em_iterations: Max EM iterations per profile evaluation.
        coordinate_grid_intervals: Log-spaced grid points for the global
            lifetime search. Lower is faster; 8 is typically fine for
            1-2 exponentials with a decent initial guess.
        lifetime_tolerance: Brent convergence threshold on lifetime.
        likelihood_tolerance: Relative NLL improvement threshold.
        em_tolerance: EM weight convergence threshold.
    """

    def __init__(
        self,
        dt,
        irf,
        period=0.0,
        convolution_stop=-1,
        tau_min=1e-3,
        tau_max=100.0,
        tail_start=-1,
        max_outer_iterations=20,
        max_em_iterations=500,
        coordinate_grid_intervals=24,
        lifetime_tolerance=1e-4,
        likelihood_tolerance=1e-9,
        em_tolerance=1e-10,
        initial_background_fraction=0.01,
    ):
        import numpy as _np
        self._dt = float(dt)
        self._irf = _np.ascontiguousarray(irf, dtype=_np.float64)
        self._n_bins = int(self._irf.shape[0])
        self._period = float(period)
        self._convolution_stop = int(convolution_stop)
        self._tau_min = float(tau_min)
        self._tau_max = float(tau_max)
        self._tail_start = int(tail_start)
        self._opts = DecayFitNExpOptions()
        self._opts.dt = self._dt
        self._opts.period = self._period
        self._opts.convolution_stop = self._convolution_stop
        self._opts.tau_min = self._tau_min
        self._opts.tau_max = self._tau_max
        self._opts.tail_start = self._tail_start
        self._opts.max_outer_iterations = int(max_outer_iterations)
        self._opts.max_em_iterations = int(max_em_iterations)
        self._opts.coordinate_grid_intervals = int(coordinate_grid_intervals)
        self._opts.lifetime_tolerance = float(lifetime_tolerance)
        self._opts.likelihood_tolerance = float(likelihood_tolerance)
        self._opts.em_tolerance = float(em_tolerance)
        self._opts.initial_background_fraction = float(initial_background_fraction)

    def __call__(
        self,
        data,
        initial_lifetimes,
        fixed=None,
        background=None,
        initial_amplitudes=None,
        include_model=False,
    ):
        """Fit a single decay curve.

        Args:
            data: 1-D decay histogram (or 2-D for Jordi VV+VH).
            initial_lifetimes: Starting lifetime values.
            fixed: List of 0/1; 1 holds the corresponding lifetime fixed.
            background: Optional background pattern (same length as IRF).
            initial_amplitudes: Starting amplitudes (uniform if None).
            include_model: If True, include the fitted model curve.

        Returns:
            dict with keys: ``converged``, ``lifetimes``, ``amplitudes``,
            ``negative_log_likelihood``, ``photon_count``,
            ``background_amplitude``, and optionally ``model``.
        """
        import numpy as _np

        n_exp = len(initial_lifetimes)
        lifetimes = _np.asarray(initial_lifetimes, dtype=_np.float64)
        if fixed is None:
            fixed_mask = [0] * n_exp
        else:
            fixed_mask = list(fixed)
        if initial_amplitudes is None:
            amps = [1.0] * n_exp
        else:
            amps = list(initial_amplitudes)
        bg = [] if background is None else _np.ascontiguousarray(
            background, dtype=_np.float64
        )
        opts = DecayFitNExpOptions()
        opts.dt = self._dt
        opts.period = self._period
        opts.convolution_stop = self._convolution_stop
        opts.tau_min = self._tau_min
        opts.tau_max = self._tau_max
        opts.tail_start = self._tail_start
        opts.max_outer_iterations = self._opts.max_outer_iterations
        opts.max_em_iterations = self._opts.max_em_iterations
        opts.coordinate_grid_intervals = self._opts.coordinate_grid_intervals
        opts.lifetime_tolerance = self._opts.lifetime_tolerance
        opts.likelihood_tolerance = self._opts.likelihood_tolerance
        opts.em_tolerance = self._opts.em_tolerance
        opts.initial_background_fraction = self._opts.initial_background_fraction
        opts.include_model = bool(include_model)

        result = DecayFitNExp.fit_buffers(
            _np.ascontiguousarray(data, dtype=_np.float64),
            self._irf,
            bg,
            lifetimes,
            _np.asarray(amps, dtype=_np.float64),
            _np.asarray(fixed_mask, dtype=_np.int32),
            opts,
        )
        out = {
            "converged": result.converged,
            "lifetimes": list(result.lifetimes),
            "amplitudes": list(result.amplitudes),
            "negative_log_likelihood": result.negative_log_likelihood,
            "photon_count": result.photon_count,
            "background_amplitude": result.background_amplitude,
        }
        if include_model:
            out["model"] = _np.asarray(result.model, dtype=_np.float64)
        return out

    def fit_many(
        self,
        matrix,
        initial_lifetimes,
        fixed=None,
        background=None,
        initial_amplitudes=None,
    ):
        """Fit a matrix of decays (one row per decay) in batch.

        Returns an (n_rows, 4 + 2*n_exp) array with layout
        ``[nll, bg_amp, converged, outer_iter, tau_0.., amp_0..]``.
        """
        import numpy as _np

        m = _np.ascontiguousarray(matrix, dtype=_np.float64)
        if m.ndim != 2:
            raise ValueError("matrix must be 2-D (n_rows, n_cols)")
        n_rows, n_cols = m.shape
        n_exp = len(initial_lifetimes)
        lifetimes = _np.asarray(initial_lifetimes, dtype=_np.float64)
        if fixed is None:
            fixed_mask = [0] * n_exp
        else:
            fixed_mask = list(fixed)
        if initial_amplitudes is None:
            amps = [1.0] * n_exp
        else:
            amps = list(initial_amplitudes)
        bg = _np.array([], dtype=_np.float64) if background is None else _np.ascontiguousarray(
            background, dtype=_np.float64
        )

        flat = DecayFitNExp.fit_batch_flat_buffers(
            m,
            self._irf,
            bg,
            lifetimes,
            _np.asarray(amps, dtype=_np.float64),
            _np.asarray(fixed_mask, dtype=_np.int32),
            self._opts,
        )
        width = 4 + 2 * n_exp
        return _np.asarray(flat, dtype=_np.float64).reshape(n_rows, width)

    def fit_map(
        self,
        cube,
        initial_lifetimes,
        fixed=None,
        minimum_photons=1,
        background=None,
        initial_amplitudes=None,
    ):
        """Fit every pixel of a decay image stack.

        Args:
            cube: (Y, X, H) or (Y, X, 2*H) array of per-pixel decays.
            initial_lifetimes: Starting lifetime values (shared by all pixels).
            fixed: List of 0/1; 1 holds the lifetime fixed.
            minimum_photons: Fewest photons a pixel needs to be fitted.
            background: Optional background pattern.
            initial_amplitudes: Starting amplitudes (uniform if None).

        Returns:
            dict with ``valid`` (Y, X) bool mask, ``intensity`` (Y, X),
            ``lifetimes`` (Y, X, n_exp), ``amplitudes`` (Y, X, n_exp),
            ``negative_log_likelihood`` (Y, X), ``converged`` (Y, X),
            and ``outer_iterations`` (Y, X). Masked pixels are ``nan``.
        """
        import numpy as _np

        cube = _np.ascontiguousarray(cube, dtype=_np.float64)
        if cube.ndim != 3:
            raise ValueError("cube must be 3-D (Y, X, H)")
        ny, nx, n_cols = cube.shape
        n_exp = len(initial_lifetimes)
        flat = cube.reshape(ny * nx, n_cols)
        intensity = flat.sum(axis=1)
        valid = intensity >= float(minimum_photons)

        width = 4 + 2 * n_exp
        result = _np.full((ny * nx, width), _np.nan)

        rows = _np.flatnonzero(valid)
        if rows.size:
            batch = self.fit_many(
                flat[rows],
                initial_lifetimes,
                fixed=fixed,
                background=background,
                initial_amplitudes=initial_amplitudes,
            )
            result[rows] = batch

        result = result.reshape(ny, nx, width)
        return {
            "valid": valid.reshape(ny, nx),
            "intensity": intensity.reshape(ny, nx),
            "negative_log_likelihood": result[:, :, 0],
            "background_amplitude": result[:, :, 1],
            "converged": result[:, :, 2].astype(bool),
            "outer_iterations": result[:, :, 3],
            "lifetimes": result[:, :, 4:4 + n_exp],
            "amplitudes": result[:, :, 4 + n_exp:4 + 2 * n_exp],
        }
