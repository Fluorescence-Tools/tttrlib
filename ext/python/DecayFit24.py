import numpy as np
import tttrlib


class Fit24(tttrlib.Fit2x):

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
        twoIstar = tttrlib.DecayFit24.fit(x, fixed, self._m_param)
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

