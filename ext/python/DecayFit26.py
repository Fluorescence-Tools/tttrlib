import numpy as np
import tttrlib


class Fit26(tttrlib.Fit2x):

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
        self._m_param = tttrlib.CreateMParam(
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
        twoIstar = tttrlib.DecayFit26.fit(x, fixed, self._m_param)
        r = dict()
        r['x'] = x
        r['fixed'] = fixed
        r['twoIstar'] = twoIstar
        if include_model:
            r['model'] = self.model
        return r

