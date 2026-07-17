# SPDX-License-Identifier: BSD-3-Clause
class Fit2x(object):

    def __init__(
            self,
            dt,                           # type: float
            irf,                          # type: np.ndarray
            background,                   # type: np.ndarray
            period,                       # type: float
            g_factor = 1.0,               # type: float
            l1 = 0.0,                     # type: float
            l2 = 0.0,                     # type: float
            convolution_stop = -1,        # type: int
            soft_bifl_scatter_flag = True,# type: bool
            verbose = False,              # type: bool
            p2s_twoIstar_flag = False     # type: bool
    ):
        """
        :param dt: time difference between microtime bins
        :param irf: counting histogram of the instrument response function in
        Jordi format
        :param background: background counting histogram in Jordi format
        :param period: excitation period of the light source
        :param g_factor: g-factor, only used for calculation of anisotropy by
        intensity in Ss and Sp
        :param l1: factor correcting mixing between parallel and perpendicular
        detection channel, only used for calculation of anisotropy by
        intensity in Ss and Sp
        :param l2: factor correcting mixing between parallel and perpendicular
        detection channel, only used for calculation of anisotropy by
        intensity in Ss and Sp
        :param convolution_stop: maximum micro time channel for comvolution. If
        no value is provided the length of the IRF is used
        :param soft_bifl_scatter_flag: if set to True the returned Istar value
        is reduced by the background photon contribution (background photons do
        no inform on the fluorescence lifetime)
        :param p2s_twoIstar_flag: If this is set to True the sum decay composed
        by P + 2S (P - Parallel, S - Perpendicular) is optimized. Otherwise
        (default) the decays P and S are optimized individually in a global fit.
        :param verbose: if set to True is more verbose
        """
        if len(irf) != len(background):
            raise ValueError("The IRF and the background differ in size")
        if len(irf) % 2 != 0:
            raise ValueError("The length of the input arrays is not divisible "
                             "by two. Inputs need to be in Jordi format.")
        if convolution_stop < 0:
            convolution_stop = len(irf) // 2 - 1
        self._bifl_scatter = -1 if soft_bifl_scatter_flag else 0
        self._p_2s_flag = p2s_twoIstar_flag
        self._corrections = np.array([period, g_factor, l1, l2, convolution_stop])
        self._irf = irf
        self._background = background
        self._dt = dt
        self._m_param = DecayFitData(
            dt=self._dt,
            corrections=self._corrections,
            irf=self._irf,
            background=self._background
        )
        self._verbose = verbose

    @property
    def model(self):
        return np.array([x for x in self._m_param.get_model()])

    @property
    def data(self):
        return np.array([x for x in self._m_param.get_data()])

    @property
    def irf(self):
        return np.array([x for x in self._m_param.get_irf()])

    @property
    def background(self):
        return np.array([x for x in self._m_param.get_background()])

    def __call__(self, data):
        # type: (np.ndarray) -> None
        """Take care of data ana make sure that it is a numpy object
        with appropriate dtype

        :param data:
        :return:
        """
        self._m_param.set_data(
            np.array(data).astype(dtype=np.int32)
        )



class Fit23(Fit2x):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def __call__(
            self,
            data, # type: np.ndarray
            initial_values, # type: np.ndarray
            fixed = None, # type: np.ndarray
            include_model = False # type: bool
    ):
        # type: (...) ->  dict
        """

        :param data: counting histogram containing experimental data
        :param initial_values: initial values of the model parameters that can
        be optimized. [tau, gamma, r0, rho]
        :param fixed: optional array of short (16bit) integers that specifies if
        a parameter is fixed. Parameters that are fixed are not optimized.
        :param include_model: if set to True (default is False) the realization
        of the model that corresponds to the optimized parameters is included in
        the returned dictionary.
        :return: dictionary containing a quality parameter (key: "Istar"), the
        corresponding optimized model parameter values (key: "x"), and an array
        which parameters were fixed (key: "fixed").
        """
        super().__call__(data=data)
        if len(initial_values) < 4:
            raise ValueError(
                "Provide initial values for all for all 4 fitting "
                "parameters."
            )
        if fixed is None:
            # lifetime free
            fixed = np.array([0, 1, 1, 1], dtype=np.int16)
        elif isinstance(fixed, np.ndarray):
            if len(fixed) < 4:
                raise ValueError(
                    "The fixed array is too short. Specify for all 4 fitting "
                    "parameters if they are fixed."
                )
        else:
            raise ValueError(
                "The fixed array is of the wrong type. Use an numpy array of "
                "length 4 to specify the fixed state for all 4 model "
                "parameters."
            )
        r = dict()
        x = np.zeros(8, dtype=np.float64)
        x[:4] = initial_values
        x[4] = self._bifl_scatter
        x[5] = self._p_2s_flag
        # the other x values are used as outputs
        fixed = fixed.astype(dtype=np.int16)
        twoIstar = DecayFit23.fit(x, fixed, self._m_param)
        r['x'] = x
        r['fixed'] = fixed
        r['twoIstar'] = twoIstar
        if include_model:
            r['model'] = self.model
        return r

    def fit_many(
            self,
            data,
            initial_values,
            fixed=None,
            include_anisotropy=False
    ):
        """Fit a matrix of decays with the Poisson maximum likelihood model.

        The shared IRF, background, correction factors, starting values, and
        fixed-parameter mask are prepared once. The native batch kernel then
        fits rows in parallel without Python callback or per-row wrapper
        overhead. For the common unpolarized single-lifetime case
        (``gamma=r0=0`` and only ``tau`` free), an algebraically equivalent
        fused one-channel objective is selected automatically.

        Parameters
        ----------
        data : numpy.ndarray
            ``(n_decays, 2*n_channels)`` VV/VH histograms in Jordi format.
        initial_values : array-like
            Shared ``[tau, gamma, r0, rho]`` starting values.
        fixed : array-like, optional
            Four fixed-parameter flags. The default fits only ``tau``.
        include_anisotropy : bool, optional
            Append experimental and scatter-corrected anisotropy columns.

        Returns
        -------
        numpy.ndarray
            Columns are ``tau, gamma, r0, rho, 2I*`` and, when requested,
            ``r_scatter, r_experimental``.
        """
        data_arr = np.ascontiguousarray(data, dtype=np.float64)
        if data_arr.ndim != 2:
            raise ValueError("data must be a 2-D matrix of Jordi histograms")
        if data_arr.shape[1] != len(self._irf):
            raise ValueError(
                "each data row must have the same length as the Jordi IRF"
            )

        x0 = np.ascontiguousarray(initial_values, dtype=np.float64)
        if x0.ndim != 1 or x0.size < 4:
            raise ValueError("initial_values must contain tau, gamma, r0, rho")
        if fixed is None:
            fixed_arr = np.array([0, 1, 1, 1], dtype=np.int16)
        else:
            fixed_arr = np.ascontiguousarray(fixed, dtype=np.int16)
            if fixed_arr.ndim != 1 or fixed_arr.size < 4:
                raise ValueError("fixed must contain four parameter flags")

        n_columns = 7 if include_anisotropy else 5
        out = np.empty((data_arr.shape[0], n_columns), dtype=np.float64)
        DecayFit23.fit_matrix(
            data_arr,
            x0,
            fixed_arr,
            float(self._bifl_scatter),
            float(self._p_2s_flag),
            self._m_param,
            out,
        )
        return out

    def fit_map(
            self,
            data,
            initial_values,
            fixed=None,
            minimum_photons=1,
            include_anisotropy=True
    ):
        """Fit a polarization-resolved FLIM image with native Poisson MLE.

        ``data`` is a ``(rows, columns, 2*n_channels)`` Jordi decay cube.
        Pixels below ``minimum_photons`` are left as NaN. The returned mapping
        contains intensity, validity, fitted parameters, and fit quality maps.
        """
        cube = np.ascontiguousarray(data, dtype=np.float64)
        if cube.ndim != 3:
            raise ValueError(
                "data must be a (rows, columns, 2*n_channels) decay cube"
            )
        if cube.shape[2] != len(self._irf):
            raise ValueError(
                "the decay axis must have the same length as the Jordi IRF"
            )
        if minimum_photons < 0:
            raise ValueError("minimum_photons must be non-negative")

        image_shape = cube.shape[:2]
        flat = cube.reshape(-1, cube.shape[2])
        intensity = flat.sum(axis=1)
        valid = intensity >= minimum_photons
        n_columns = 7 if include_anisotropy else 5
        values = np.full((flat.shape[0], n_columns), np.nan, dtype=np.float64)
        if np.any(valid):
            values[valid] = self.fit_many(
                flat[valid],
                initial_values,
                fixed=fixed,
                include_anisotropy=include_anisotropy,
            )

        result = {
            "intensity": intensity.reshape(image_shape),
            "valid": valid.reshape(image_shape),
            "tau": values[:, 0].reshape(image_shape),
            "gamma": values[:, 1].reshape(image_shape),
            "r0": values[:, 2].reshape(image_shape),
            "rho": values[:, 3].reshape(image_shape),
            "twoIstar": values[:, 4].reshape(image_shape),
        }
        if include_anisotropy:
            result["r_scatter"] = values[:, 5].reshape(image_shape)
            result["r_experimental"] = values[:, 6].reshape(image_shape)
        return result


class FitNExp(Fit2x):
    """General one- or multi-exponential Poisson reconvolution fitter.

    Unlike the polarization/anisotropy-specific Fit23 model, this class uses a
    shared temporal shape with any number of exponential components. Input may
    be a single decay or Jordi VV/VH data; Jordi channels are pooled as exact
    sufficient statistics while their independently profiled totals are
    retained in the returned model. All optimization runs in native C++.
    """

    def __init__(
            self,
            dt,
            irf,
            background=None,
            period=0.0,
            convolution_stop=-1,
            tau_min=1.0e-3,
            tau_max=100.0,
            lifetime_tolerance=1.0e-4,
            likelihood_tolerance=1.0e-9,
            em_tolerance=1.0e-10,
            max_outer_iterations=20,
            max_em_iterations=500,
            initial_background_fraction=0.01,
            coordinate_grid_intervals=24,
    ):
        self._irf = np.ascontiguousarray(irf, dtype=np.float64)
        if self._irf.ndim != 1 or self._irf.size == 0:
            raise ValueError("irf must be a non-empty one-dimensional array")
        if background is None:
            self._background = np.empty(0, dtype=np.float64)
        else:
            self._background = np.ascontiguousarray(
                background, dtype=np.float64
            )
            if self._background.ndim != 1:
                raise ValueError("background must be one-dimensional")
            if self._background.size not in (0, self._irf.size):
                raise ValueError("background and irf lengths must match")

        self._options = DecayFitNExpOptions()
        self._options.dt = float(dt)
        self._options.period = float(period)
        self._options.convolution_stop = int(convolution_stop)
        self._options.tau_min = float(tau_min)
        self._options.tau_max = float(tau_max)
        self._options.lifetime_tolerance = float(lifetime_tolerance)
        self._options.likelihood_tolerance = float(likelihood_tolerance)
        self._options.em_tolerance = float(em_tolerance)
        self._options.max_outer_iterations = int(max_outer_iterations)
        self._options.max_em_iterations = int(max_em_iterations)
        self._options.coordinate_grid_intervals = int(coordinate_grid_intervals)
        self._options.initial_background_fraction = float(
            initial_background_fraction
        )
        self._last_data = np.empty(0, dtype=np.float64)
        self._last_model = np.empty(0, dtype=np.float64)

    def _call_options(self, include_model):
        """Return an isolated native options object for one fit call."""
        options = DecayFitNExpOptions()
        for name in (
                "dt", "period", "convolution_stop", "tau_min", "tau_max",
                "lifetime_tolerance", "likelihood_tolerance",
                "em_tolerance", "max_outer_iterations", "max_em_iterations",
                "coordinate_grid_intervals", "initial_background_fraction",
        ):
            setattr(options, name, getattr(self._options, name))
        options.include_model = bool(include_model)
        return options

    @property
    def data(self):
        return self._last_data.copy()

    @property
    def model(self):
        return self._last_model.copy()

    @property
    def irf(self):
        return self._irf.copy()

    @property
    def background(self):
        return self._background.copy()

    @staticmethod
    def _component_inputs(initial_lifetimes, initial_amplitudes, fixed):
        lifetimes = np.ascontiguousarray(
            initial_lifetimes, dtype=np.float64
        )
        if lifetimes.ndim != 1 or lifetimes.size == 0:
            raise ValueError("initial_lifetimes must contain at least one value")
        if initial_amplitudes is None:
            amplitudes = np.ones(lifetimes.size, dtype=np.float64)
        else:
            amplitudes = np.ascontiguousarray(
                initial_amplitudes, dtype=np.float64
            )
        if amplitudes.ndim != 1 or amplitudes.size != lifetimes.size:
            raise ValueError("amplitudes and lifetimes must have equal length")
        if fixed is None:
            fixed_arr = np.zeros(lifetimes.size, dtype=np.int32)
        else:
            fixed_arr = np.ascontiguousarray(fixed, dtype=np.int32)
        if fixed_arr.ndim != 1 or fixed_arr.size != lifetimes.size:
            raise ValueError("fixed and lifetimes must have equal length")
        return lifetimes, amplitudes, fixed_arr

    @staticmethod
    def _result_dict(result, include_model):
        output = {
            "lifetimes": np.asarray(result.lifetimes, dtype=np.float64),
            "amplitudes": np.asarray(result.amplitudes, dtype=np.float64),
            "background_amplitude": float(result.background_amplitude),
            "negative_log_likelihood": float(
                result.negative_log_likelihood
            ),
            "photon_count": float(result.photon_count),
            "converged": bool(result.converged),
            "outer_iterations": int(result.outer_iterations),
            "em_iterations": int(result.em_iterations),
        }
        if include_model:
            output["model"] = np.asarray(result.model, dtype=np.float64)
        return output

    def __call__(
            self,
            data,
            initial_lifetimes,
            initial_amplitudes=None,
            fixed=None,
            include_model=False,
    ):
        """Fit one decay with arbitrary component count."""
        data_arr = np.ascontiguousarray(data, dtype=np.float64)
        if data_arr.ndim != 1 or data_arr.size not in (
                self._irf.size, 2 * self._irf.size):
            raise ValueError("data must contain one decay or Jordi VV/VH data")
        lifetimes, amplitudes, fixed_arr = self._component_inputs(
            initial_lifetimes, initial_amplitudes, fixed
        )
        result = DecayFitNExp.fit_buffers(
            data_arr,
            self._irf,
            self._background,
            lifetimes,
            amplitudes,
            fixed_arr,
            self._call_options(include_model),
        )
        self._last_data = data_arr
        self._last_model = np.asarray(result.model, dtype=np.float64)
        return self._result_dict(result, include_model)

    def fit_fixed_lifetimes(
            self,
            data,
            lifetimes,
            initial_amplitudes=None,
            include_model=False,
    ):
        """Profile nonnegative amplitudes by EM without lifetime searches."""
        data_arr = np.ascontiguousarray(data, dtype=np.float64)
        if data_arr.ndim != 1 or data_arr.size not in (
                self._irf.size, 2 * self._irf.size):
            raise ValueError("data must contain one decay or Jordi VV/VH data")
        lifetimes, amplitudes, _ = self._component_inputs(
            lifetimes, initial_amplitudes,
            np.ones(len(lifetimes), dtype=np.int32),
        )
        result = DecayFitNExp.fit_fixed_lifetimes_buffers(
            data_arr,
            self._irf,
            self._background,
            lifetimes,
            amplitudes,
            self._call_options(include_model),
        )
        self._last_data = data_arr
        self._last_model = np.asarray(result.model, dtype=np.float64)
        return self._result_dict(result, include_model)

    def fit_many(
            self,
            data,
            initial_lifetimes,
            initial_amplitudes=None,
            fixed=None,
    ):
        """Fit a matrix in the native threaded C++ batch engine.

        Output columns are ``nll, background_amplitude, converged,
        outer_iterations, lifetimes..., amplitudes...``.
        """
        matrix = np.ascontiguousarray(data, dtype=np.float64)
        if matrix.ndim != 2 or matrix.shape[1] not in (
                self._irf.size, 2 * self._irf.size):
            raise ValueError(
                "data must be a matrix of single-channel or Jordi decays"
            )
        lifetimes, amplitudes, fixed_arr = self._component_inputs(
            initial_lifetimes, initial_amplitudes, fixed
        )
        flat_output = DecayFitNExp.fit_batch_flat(
            matrix.ravel().tolist(),
            matrix.shape[0],
            matrix.shape[1],
            self._irf.tolist(),
            self._background.tolist(),
            lifetimes.tolist(),
            amplitudes.tolist(),
            fixed_arr.tolist(),
            self._options,
        )
        width = 4 + 2 * lifetimes.size
        return np.asarray(flat_output, dtype=np.float64).reshape(-1, width)

    def fit_map(
            self,
            data,
            initial_lifetimes,
            initial_amplitudes=None,
            fixed=None,
            minimum_photons=1,
    ):
        """Fit an image of decays with arbitrary exponential count."""
        cube = np.ascontiguousarray(data, dtype=np.float64)
        if cube.ndim != 3 or cube.shape[2] not in (
                self._irf.size, 2 * self._irf.size):
            raise ValueError(
                "data must be a (rows, columns, decay_channels) cube"
            )
        if minimum_photons < 0:
            raise ValueError("minimum_photons must be non-negative")
        lifetimes, amplitudes, fixed_arr = self._component_inputs(
            initial_lifetimes, initial_amplitudes, fixed
        )
        shape = cube.shape[:2]
        flat = cube.reshape(-1, cube.shape[2])
        intensity = flat.sum(axis=1)
        valid = intensity >= minimum_photons
        values = np.full(
            (flat.shape[0], 4 + 2 * lifetimes.size),
            np.nan,
            dtype=np.float64,
        )
        if np.any(valid):
            values[valid] = self.fit_many(
                flat[valid], lifetimes, amplitudes, fixed_arr
            )
        n_exp = lifetimes.size
        return {
            "intensity": intensity.reshape(shape),
            "valid": valid.reshape(shape),
            "negative_log_likelihood": values[:, 0].reshape(shape),
            "background_amplitude": values[:, 1].reshape(shape),
            "converged": (values[:, 2] == 1.0).reshape(shape),
            "outer_iterations": values[:, 3].reshape(shape),
            "lifetimes": values[:, 4:4 + n_exp].reshape(*shape, n_exp),
            "amplitudes": values[:, 4 + n_exp:].reshape(*shape, n_exp),
        }


class Fit24(Fit2x):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._parameter_names = [
            'tau1', 'gamma', 'tau2', 'a2',
            'offset / background'
        ]

    def __call__(
            self,
            data, # type: np.ndarray
            initial_values, # type: np.ndarray
            fixed = None, # type: np.ndarray
            include_model = False # type: bool
    ):
        # type: (...) -> dict
        """
        :param data: counting histogram containing experimental data
        :param initial_values: initial values of the model parameters that can
        be optimized. [tau1, gamma, tau2, A2, offset]
        :param fixed: optional array of short (16bit) integers that specifies if
        a parameter is fixed. Parameters that are fixed are not optimized.
        :param include_model: if set to True (default is False) the realization
        of the model that corresponds to the optimized parameters is included in
        the returned dictionary.
        :return: dictionary containing a quality parameter (key: "twoIstar"), the
        corresponding optimized model parameter values (key: "x"), and an array
        which parameters were fixed (key: "fixed").
        """
        super().__call__(data=data)
        if len(initial_values) < 5:
            raise ValueError(
                "Provide initial values for all for all 4 fitting "
                "parameters."
            )
        if fixed is None:
            # lifetime free
            fixed = np.array([0, 0, 0, 0, 0], dtype=np.int16)
        elif isinstance(fixed, np.ndarray):
            if len(fixed) < 5:
                raise ValueError(
                    "The fixed array is too short. Specify for all 5 fitting "
                    "parameters if they are fixed."
                )
        else:
            raise ValueError(
                "The fixed array is of the wrong type. Use an numpy array of "
                "length 5 to specify the fixed state for all 5 model "
                "parameters."
            )
        bifl_scatter = self._bifl_scatter
        x = np.zeros(8, dtype=np.float64)
        x[:5] = initial_values
        x[5] = bifl_scatter
        fixed = fixed.astype(dtype=np.int16)
        twoIstar = DecayFit24.fit(x, fixed, self._m_param)
        if self._verbose:
            print("Fitting")
            print("Parameter names: ", self._parameter_names)
            print("initial_values: ", initial_values)
            print("fixed: ", fixed)
            print("include_model: ", include_model)
            print("x0: ", x)
        r = dict()
        r['x'] = x
        r['fixed'] = fixed
        r['twoIstar'] = twoIstar
        if include_model:
            r['model'] = self.model
        return r


class Fit25(Fit2x):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)
        self._parameter_names = [
            'tau1', 'tau2', 'tau3', 'tau4', 'gamma'
        ]

    def __call__(
            self,
            data, # type: np.ndarray
            initial_values, # type: np.ndarray
            fixed = None, # type: np.ndarray
            include_model = False # type: bool
    ):
        # type: (...) -> dict
        """
        :param data: counting histogram containing experimental data
        :param initial_values: initial values of the model parameters that can
        be optimized. [tau1, tau2, tau3, tau4, gamma]. The lifetimes are always
        fixed and not optimized. The lifetime best describing the data is
        returned.
        :param fixed: optional array of short (16bit) integers that specifies if
        a parameter is fixed. Parameters that are fixed are not optimized.
        :param include_model: if set to True (default is False) the realization
        of the model that corresponds to the optimized parameters is included in
        the returned dictionary.
        :return: dictionary containing a quality parameter (key: "twoIstar"), the
        corresponding optimized model parameter values (key: "x"), and an array
        which parameters were fixed (key: "fixed").
        """
        super().__call__(data=data)
        if len(initial_values) < 5:
            raise ValueError(
                "Provide initial values for all for all 6 fitting "
                "parameters."
            )
        if fixed is None:
            fixed = np.array([0, 0, 0, 0, 0], dtype=np.int16)
        elif isinstance(fixed, np.ndarray):
            if len(fixed) < 5:
                raise ValueError(
                    "The fixed array is too short. Specify for all 6 fitting "
                    "parameters if they are fixed."
                )
        else:
            raise ValueError(
                "The fixed array is of the wrong type. Use an numpy array of "
                "length 5 to specify the fixed state for all 6 model "
                "parameters."
            )
        bifl_scatter = self._bifl_scatter
        x = np.zeros(9, dtype=np.float64)
        if self._verbose:
            print("Fitting")
            print("Parameter names: ", self._parameter_names)
            print("initial_values: ", initial_values)
            print("fixed: ", fixed)
            print("include_model: ", include_model)
        x[:6] = initial_values
        x[6] = bifl_scatter
        if self._verbose:
            print("x0: ", x)
        fixed = fixed.astype(dtype=np.int16)
        twoIstar = DecayFit25.fit(x, fixed, self._m_param)
        r = dict()
        r['x'] = x
        r['fixed'] = fixed
        r['twoIstar'] = twoIstar
        if include_model:
            r['model'] = self.model
        return r


class Fit26(Fit2x):

    def __init__(
            self,
            pattern_1, # type: np.ndarray
            pattern_2, # type: np.ndarray
            convolution_stop = -1, # type: int
            verbose = False # type: bool
    ):
        if convolution_stop < 0:
            convolution_stop = min(len(pattern_1), len(pattern_2))
        self._corrections = np.array([1., 1.0, 0.0, 0.0, convolution_stop])
        self._irf = pattern_1
        self._background = pattern_2
        self._m_param = DecayFitData(
            dt=1.0,
            corrections=self._corrections,
            irf=self._irf,
            background=self._background
        )
        self._verbose = verbose
        self._parameter_names = ['x1']

    def __call__(
            self,
            data, # type: np.ndarray
            initial_values, # type: np.ndarray
            fixed = None, # type: np.ndarray
            include_model = False # type: bool
    ): 
        # type: (...) ->  dict
        """
        :param data: counting histogram containing experimental data
        :param initial_values: initial values of the model parameters that can
        be optimized. Only the fraction of the first pattern [x1] can be optimized.
        :param fixed: optional array of short (16bit) integers that specifies if
        the fraction x1 is fixed.
        :param include_model: if set to True (default is False) the realization
        of the model that corresponds to the optimized parameters is included in
        the returned dictionary.
        :return: dictionary containing a quality parameter (key: "twoIstar"), the
        corresponding optimized model parameter values (key: "x"), and an array
        which parameters were fixed (key: "fixed").
        """
        super().__call__(data=data)
        if len(initial_values) < 1:
            raise ValueError(
                "Provide initial values for all for all 6 fitting "
                "parameters."
            )
        if fixed is None:
            fixed = np.array([0], dtype=np.int16)
        elif isinstance(fixed, np.ndarray):
            if len(fixed) < 1:
                raise ValueError(
                    "The fixed array is too short. Specify for all 6 fitting "
                    "parameters if they are fixed."
                )
        else:
            raise ValueError(
                "The fixed array is of the wrong type. Use an numpy array of "
                "length 5 to specify the fixed state for all 6 model "
                "parameters."
            )
        # x[0]: fraction of pattern 1 (in/out); x[1]: 1 - x[0] (output).
        # fit26 writes both - a 1-element array would overflow.
        x = np.zeros(2, dtype=np.float64)
        x[0] = initial_values[0]
        if self._verbose:
            print("Fitting")
            print("Parameter names: ", self._parameter_names)
            print("initial_values: ", initial_values)
            print("fixed: ", fixed)
            print("include_model: ", include_model)
        if self._verbose:
            print("x0: ", x)
        fixed = fixed.astype(dtype=np.int16)
        twoIstar = DecayFit26.fit(x, fixed, self._m_param)
        r = dict()
        r['x'] = x
        r['fixed'] = fixed
        r['twoIstar'] = twoIstar
        if include_model:
            r['model'] = self.model
        return r



# ---------------------------------------------------------------------------
# One-call convenience API
#
# These functions wrap the Fit23/Fit24/Fit25/Fit26 classes so a fit is a
# single function call with plain numpy arrays and keyword arguments -- no
# MParam / LabView structures and no Jordi-format bookkeeping beyond the
# input arrays themselves.
# ---------------------------------------------------------------------------

def fit23(
        data, irf, background, dt, period,
        g_factor=1.0, l1=0.0, l2=0.0, convolution_stop=-1,
        tau=3.0, gamma=0.01, r0=0.38, rho=1.0,
        fixed=(0, 1, 1, 1),
        soft_bifl_scatter=True, p2s_twoIstar=False,
        include_model=False
):
    """Fit a single fluorescence lifetime by MLE (fit23).

    :param data: measured counting histogram (Jordi format: parallel followed
        by perpendicular channels)
    :param irf: instrument response counting histogram (Jordi format)
    :param background: background counting histogram (Jordi format)
    :param dt: width of a micro time channel
    :param period: excitation period of the light source
    :param g_factor, l1, l2: polarization corrections
    :param convolution_stop: last micro time channel of the convolution
        (default: half the array length)
    :param tau, gamma, r0, rho: initial values of the model parameters
        (lifetime, scattered fraction, fundamental anisotropy, rotation time)
    :param fixed: which of [tau, gamma, r0, rho] stay fixed (1 = fixed)
    :param soft_bifl_scatter: reduce Istar by the background contribution
    :param p2s_twoIstar: optimize P + 2S instead of P and S individually
    :param include_model: include the fitted model curve in the result
    :return: dict with keys "x" ([tau, gamma, r0, rho, ...]), "fixed",
        "twoIstar" and optionally "model"
    """
    f = Fit23(
        dt=dt, irf=np.asarray(irf, dtype=np.float64),
        background=np.asarray(background, dtype=np.float64),
        period=period, g_factor=g_factor, l1=l1, l2=l2,
        convolution_stop=convolution_stop,
        soft_bifl_scatter_flag=soft_bifl_scatter,
        p2s_twoIstar_flag=p2s_twoIstar
    )
    return f(
        data=np.asarray(data),
        initial_values=np.array([tau, gamma, r0, rho], dtype=np.float64),
        fixed=np.array(fixed, dtype=np.int16),
        include_model=include_model
    )


def fit24(
        data, irf, background, dt, period,
        convolution_stop=-1,
        tau1=3.0, gamma=0.01, tau2=0.5, a2=0.5, offset=0.0,
        fixed=(0, 0, 0, 0, 1),
        soft_bifl_scatter=True, p2s_twoIstar=False,
        include_model=False
):
    """Fit a bi-exponential decay by MLE (fit24).

    Model parameters: two lifetimes tau1 and tau2, amplitude a2 of the second
    lifetime (a1 + a2 = 1), scattered fraction gamma and a constant offset.
    See fit23 for the shared arguments. Returns the same result dict.
    """
    f = Fit24(
        dt=dt, irf=np.asarray(irf, dtype=np.float64),
        background=np.asarray(background, dtype=np.float64),
        period=period, convolution_stop=convolution_stop,
        soft_bifl_scatter_flag=soft_bifl_scatter,
        p2s_twoIstar_flag=p2s_twoIstar
    )
    return f(
        data=np.asarray(data),
        initial_values=np.array([tau1, gamma, tau2, a2, offset], dtype=np.float64),
        fixed=np.array(fixed, dtype=np.int16),
        include_model=include_model
    )


def fit25(
        data, irf, background, dt, period,
        g_factor=1.0, l1=0.0, l2=0.0, convolution_stop=-1,
        taus=(0.5, 1.0, 2.0, 4.0), gamma=0.01, r0=0.38,
        fit_gamma=True,
        soft_bifl_scatter=True, p2s_twoIstar=False,
        include_model=False
):
    """Select the lifetime out of four candidates that best describes the
    data (fit25).

    :param taus: the four candidate lifetimes
    :param gamma: initial scattered fraction (optimized when fit_gamma)
    :param r0: fundamental anisotropy
    See fit23 for the shared arguments. The best lifetime is returned in
    result["x"][0].
    """
    f = Fit25(
        dt=dt, irf=np.asarray(irf, dtype=np.float64),
        background=np.asarray(background, dtype=np.float64),
        period=period, g_factor=g_factor, l1=l1, l2=l2,
        convolution_stop=convolution_stop,
        soft_bifl_scatter_flag=soft_bifl_scatter,
        p2s_twoIstar_flag=p2s_twoIstar
    )
    t = list(taus)
    if len(t) != 4:
        raise ValueError("fit25 needs exactly four candidate lifetimes")
    return f(
        data=np.asarray(data),
        initial_values=np.array(t + [gamma, r0], dtype=np.float64),
        fixed=np.array([1, 1, 1, 1, 0 if fit_gamma else 1], dtype=np.int16),
        include_model=include_model
    )


def fit26(
        data, pattern_1, pattern_2,
        fraction_1=0.5,
        include_model=False
):
    """Fit the fraction of a mixture of two patterns (fit26).

    :param data: measured counting histogram
    :param pattern_1: first reference pattern
    :param pattern_2: second reference pattern
    :param fraction_1: initial fraction of the first pattern
    :return: dict; the fitted fraction is result["x"][0]
    """
    f = Fit26(
        pattern_1=np.asarray(pattern_1, dtype=np.float64),
        pattern_2=np.asarray(pattern_2, dtype=np.float64)
    )
    return f(
        data=np.asarray(data),
        initial_values=np.array([fraction_1], dtype=np.float64),
        include_model=include_model
    )
