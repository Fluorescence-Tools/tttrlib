class Fit2x(object):

    def __init__(
            self,
            dt: float,
            irf: np.ndarray,
            background: np.ndarray,
            period: float,
            g_factor: float = 1.0,
            l1: float = 0.0,
            l2: float = 0.0,
            convolution_stop: int = -1,
            soft_bifl_scatter_flag: bool = True,
            verbose: bool = False,
            p2s_twoIstar_flag: bool = False
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
        self._m_param = CreateMParam(
            irf=self._irf,
            background=self._background,
            corrections=self._corrections,
            dt=self._dt
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

    def __call__(
            self,
            data: np.ndarray
    ) -> None:
        """Take care of data ana make sure that it is a numpy object
        with appropriate dtype

        :param data:
        :return:
        """
        self._m_param.get_data().set_data(
            np.array(data).astype(dtype=np.int32)
        )



class Fit23(Fit2x):

    def __init__(self, *args, **kwargs):
        super().__init__(*args, **kwargs)

    def __call__(
            self,
            data: np.ndarray,
            initial_values: np.ndarray,
            fixed: np.ndarray = None,
            include_model: bool = False
    ) -> dict:
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
            data: np.ndarray,
            initial_values: np.ndarray,
            fixed: np.ndarray = None,
            include_model: bool = False
    ) -> dict:
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
            data: np.ndarray,
            initial_values: np.ndarray,
            fixed: np.ndarray = None,
            include_model: bool = False
    ) -> dict:
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
            pattern_1: np.ndarray,
            pattern_2: np.ndarray,
            convolution_stop: int = -1,
            verbose: bool = False
    ):
        if convolution_stop < 0:
            convolution_stop = min(len(pattern_1), len(pattern_2))
        self._corrections = np.array([1., 1.0, 0.0, 0.0, convolution_stop])
        self._irf = pattern_1
        self._background = pattern_2
        self._m_param = CreateMParam(
            irf=self._irf,
            background=self._background,
            corrections=self._corrections,
            dt=1.0
        )
        self._verbose = verbose
        self._parameter_names = ['x1']

    def __call__(
            self,
            data: np.ndarray,
            initial_values: np.ndarray,
            fixed: np.ndarray = None,
            include_model: bool = False
    ) -> dict:
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

