import numpy as np
import tttrlib


class Fit23(tttrlib.Fit2x):

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
        twoIstar = tttrlib.DecayFit23.fit(x, fixed, self._m_param)
        r['x'] = x
        r['fixed'] = fixed
        r['twoIstar'] = twoIstar
        if include_model:
            r['model'] = self.model
        return r

