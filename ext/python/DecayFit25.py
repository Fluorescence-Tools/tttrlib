import numpy as np
import tttrlib


class Fit25(tttrlib.Fit2x):

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
        twoIstar = tttrlib.DecayFit25.fit(x, fixed, self._m_param)
        r = dict()
        r['x'] = x
        r['fixed'] = fixed
        r['twoIstar'] = twoIstar
        if include_model:
            r['model'] = self.model
        return r

