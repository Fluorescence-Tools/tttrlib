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
        x = np.zeros(1, dtype=np.float64)
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
