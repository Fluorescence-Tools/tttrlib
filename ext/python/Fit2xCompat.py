# SPDX-License-Identifier: BSD-3-Clause
"""Deprecated Python compatibility layer for the pre-interface fit API.

`Fit23`, `Fit24`, `Fit25` and `Fit26` used to be classes built from an IRF and a
pile of correction factors, then called with a packed parameter vector whose
layout differed per estimator and mixed optimised parameters with setup flags
and outputs. They are now one interface — :class:`DecayFit2`, built by registry
name — and this module reconstructs the old surface on top of it so existing
Python code keeps running while it migrates.

**This is a shim, and it is Python-only.** It exists to decouple the timing of
the C++ change from the timing of every caller's change, not to preserve the old
design. The other language bindings were not given one. Everything here emits
:class:`DeprecationWarning` and **will be removed in tttrlib 0.29**; port to
:class:`DecayFit2`, where the parameter, setup and result layouts come from the
registry instead of from a comment.

What the replacement looks like::

    # old
    fit = tttrlib.Fit23(dt=dt, irf=irf, background=bg, period=period)
    res = fit(data, initial_values=[tau, gamma, r0, rho], fixed=[0, 1, 1, 1])
    tau, two_istar = res["x"][0], res["twoIstar"]

    # new
    setup = tttrlib.setup_vector("fit23", dt=dt, period=period)
    fit = tttrlib.DecayFit2("fit23", setup, irf)
    problem = tttrlib.DecayFitProblem(2, len(irf) // 2, dt)
    ...
    out = fit.fit([tau, gamma, r0, rho], constraints, problem)
    tau, two_istar = out.parameters[0], out.objective
"""
import warnings as _warnings


#: Release in which this whole module goes away. Stated in every warning so the
#: deadline travels with the message rather than living in a changelog nobody
#: reads at the call site.
REMOVED_IN = "0.29"


def _warn(what, instead):
    _warnings.warn(
        f"{what} is deprecated and will be removed in tttrlib {REMOVED_IN}; "
        f"use {instead}. The parameter, setup and result layouts are described "
        "by the registry (see tttrlib.registry('fit')).",
        DeprecationWarning,
        stacklevel=3,
    )


#: Old packed-``x`` width and where the estimator wrote its extra slots.
#: value = (name, n_parameters, x_width, bifl_index, p2s_index, output_index)
#: ``None`` means the estimator had no such slot.
_LAYOUT = {
    "Fit23": ("fit23", 4, 8, 4, 5, 6),
    "Fit24": ("fit24", 5, 8, 5, None, 6),
    "Fit25": ("fit25", 6, 9, 6, None, 7),
    "Fit26": ("fit26", 1, 2, None, None, None),
}


class _CompatFit(object):
    """Base of the deprecated estimator classes.

    Holds a :class:`DecayFit2` and a :class:`DecayFitProblem` and translates the
    old call convention onto them.
    """

    _NAME = "Fit23"

    def __init__(
            self,
            dt,
            irf,
            background,
            period,
            g_factor=1.0,
            l1=0.0,
            l2=0.0,
            convolution_stop=-1,
            soft_bifl_scatter_flag=True,
            verbose=False,
            p2s_twoIstar_flag=False,
    ):
        import numpy as np

        _warn(f"tttrlib.{type(self).__name__}", "tttrlib.DecayFit2")

        irf = np.ascontiguousarray(irf, dtype=np.float64)
        background = np.ascontiguousarray(background, dtype=np.float64)
        if irf.size != background.size:
            raise ValueError("The IRF and the background differ in size")
        if irf.size % 2 != 0:
            raise ValueError(
                "The length of the input arrays is not divisible by two. "
                "Inputs need to be in Jordi format.")

        name, n_par, x_width, bifl_index, p2s_index, out_index = _LAYOUT[
            type(self).__name__]
        self._name = name
        self._n_parameters = n_par
        self._x_width = x_width
        self._bifl_index = bifl_index
        self._p2s_index = p2s_index
        self._out_index = out_index
        self._n_bins = irf.size // 2
        self._verbose = verbose

        # The old flags, kept as attributes because callers read them.
        self._bifl_scatter = -1 if soft_bifl_scatter_flag else 0
        self._p_2s_flag = 1 if p2s_twoIstar_flag else 0

        setup = setup_vector(
            name, dt=dt, period=period, g_factor=g_factor, l1=l1, l2=l2,
            convolution_stop=convolution_stop,
            soft_bifl_scatter_flag=soft_bifl_scatter_flag,
            objective="p2s_mle" if p2s_twoIstar_flag else "poisson_mle")

        self._fit = DecayFit2(name, setup, irf.tolist())
        self._problem = DecayFitProblem(2, self._n_bins, float(dt))
        self._problem.irf = VectorDouble(irf.tolist())
        self._problem.background = VectorDouble(background.tolist())
        # Carry the setup on the problem as well as in the model. The model has
        # its own copy and does not read this one, but anything handed the bare
        # problem — the deprecated ``fit_matrix`` below, or a provenance record —
        # can then recover how the fit was configured instead of guessing at
        # defaults.
        self._problem.setup = VectorDouble(setup)

    #: The old name for the problem container; callers reached for it directly.
    @property
    def _m_param(self):
        return self._problem

    @property
    def model(self):
        import numpy as np
        return np.asarray(self._problem.model, dtype=np.float64)

    @property
    def data(self):
        import numpy as np
        return np.asarray(self._problem.data, dtype=np.float64)

    @property
    def irf(self):
        import numpy as np
        return np.asarray(self._problem.irf, dtype=np.float64)

    @property
    def background(self):
        import numpy as np
        return np.asarray(self._problem.background, dtype=np.float64)

    def _links(self, fixed):
        """Old boolean ``fixed`` mask to the link vector."""
        import numpy as np
        if fixed is None:
            codes = [0] * self._n_parameters
        else:
            mask = np.asarray(fixed).ravel()
            if mask.size < self._n_parameters:
                raise ValueError(
                    f"The fixed array is too short. Specify for all "
                    f"{self._n_parameters} fitting parameters if they are fixed.")
            codes = [-1 if int(v) else 0 for v in mask[:self._n_parameters]]
        return DecayFitConstraints(VectorInt32(codes))

    def _packed(self, parameters, results):
        """Rebuild the old wide ``x`` vector, outputs and flags included."""
        import numpy as np
        x = np.zeros(self._x_width, dtype=np.float64)
        x[:self._n_parameters] = parameters[:self._n_parameters]
        if self._bifl_index is not None:
            x[self._bifl_index] = self._bifl_scatter
        if self._p2s_index is not None:
            x[self._p2s_index] = self._p_2s_flag
        if self._out_index is not None and len(results) >= 5:
            # r_scatter and r_experimental are the last two result columns.
            x[self._out_index] = results[-2]
            x[self._out_index + 1] = results[-1]
        return x

    def __call__(self, data, initial_values=None, fixed=None, include_model=False):
        import numpy as np

        values = np.asarray(data, dtype=np.float64).ravel()
        if values.size != self._problem.total_size():
            raise ValueError(
                f"data has {values.size} channels, expected "
                f"{self._problem.total_size()}")
        self._problem.data = VectorDouble(values.tolist())

        if initial_values is None:
            raise ValueError("Provide initial values for the fitting parameters.")
        start = np.asarray(initial_values, dtype=np.float64).ravel()
        if start.size < self._n_parameters:
            raise ValueError(
                f"Provide initial values for all {self._n_parameters} "
                f"fitting parameters.")

        out = self._fit.fit(
            [float(v) for v in start[:self._n_parameters]],
            self._links(fixed), self._problem)

        parameters = list(out.parameters)
        results = list(out.results)
        record = {
            "x": self._packed(parameters, results),
            "fixed": (np.zeros(self._n_parameters, dtype=np.int16)
                      if fixed is None
                      else np.asarray(fixed, dtype=np.int16)),
            "twoIstar": out.objective,
        }
        if include_model:
            record["model"] = self.model
        return record

    def fit_many(self, data, initial_values, fixed=None, **kwargs):
        """Fit a matrix of decays; one row per decay.

        Returns the old ``(n_rows, n_parameters + 1)`` layout: the fitted
        parameters followed by 2I*.
        """
        import numpy as np

        matrix = np.ascontiguousarray(data, dtype=np.float64)
        if matrix.ndim != 2:
            raise ValueError("data must be a 2-D (n_rows, 2*n_channels) matrix")
        start = np.asarray(initial_values, dtype=np.float64).ravel()

        batch = self._fit.fit_many(
            self._problem, matrix.ravel().tolist(), int(matrix.shape[0]),
            int(matrix.shape[1]),
            [float(v) for v in start[:self._n_parameters]],
            self._links(fixed))

        n_rows = matrix.shape[0]
        parameters = np.asarray(batch.parameters).reshape(n_rows, self._n_parameters)
        out = np.empty((n_rows, self._n_parameters + 1), dtype=np.float64)
        out[:, :self._n_parameters] = parameters
        out[:, self._n_parameters] = np.asarray(batch.objective)
        return out

    def fit_map(self, cube, initial_values, fixed=None, minimum_number_of_photons=1, **kwargs):
        """Fit an image stack, masking pixels with too few photons."""
        import numpy as np

        result = fit_image(self._fit, self._problem, cube,
                           [float(v) for v in
                            np.asarray(initial_values, dtype=np.float64).ravel()[
                                :self._n_parameters]],
                           self._links(fixed),
                           min_counts=minimum_number_of_photons)
        names = result_names(self._name)
        out = {
            "valid": result["valid"],
            "intensity": result["intensity"],
            "twoIstar": result["objective"],
        }
        for index, column in enumerate(("tau", "gamma", "r0", "rho")[:self._n_parameters]):
            out[column] = result["parameters"][:, :, index]
        for index, column in enumerate(names):
            out.setdefault(column, result["results"][:, :, index])
        return out


class Fit23(_CompatFit):
    """Deprecated. Use ``DecayFit2("fit23", ...)``."""
    _NAME = "Fit23"


class Fit24(_CompatFit):
    """Deprecated. Use ``DecayFit2("fit24", ...)``."""
    _NAME = "Fit24"


class Fit25(_CompatFit):
    """Deprecated. Use ``DecayFit2("fit25", ...)``."""
    _NAME = "Fit25"


class Fit26(_CompatFit):
    """Deprecated. Use ``DecayFit2("fit26", ...)``."""
    _NAME = "Fit26"


def _batch_into(name, n_parameters, data, x0, fixed, problem, out):
    """Shared body of the deprecated ``DecayFitNN.fit_matrix`` entry points.

    ``out`` is filled in place with the fitted parameters followed by 2I*, which
    is the layout the old kernels wrote.
    """
    import numpy as np

    matrix = np.ascontiguousarray(data, dtype=np.float64)
    start = np.asarray(x0, dtype=np.float64).ravel()
    mask = np.asarray(fixed).ravel()

    setup = list(problem.setup)
    if not setup:
        raise ValueError(
            "the problem carries no setup vector, so the fit cannot be "
            "reconstructed; build it through the deprecated estimator classes "
            "or use tttrlib.DecayFit2 directly")
    fit = DecayFit2(name, setup, list(problem.irf))

    # The old callers packed the estimator's flags and extra inputs into x0 and
    # passed a shorter `fixed` mask covering only the freely fitted slots; any
    # parameter beyond that mask was held.
    codes = [-1 if (i >= mask.size or int(mask[i])) else 0
             for i in range(n_parameters)]

    batch = fit.fit_many(
        problem, matrix.ravel().tolist(), int(matrix.shape[0]),
        int(matrix.shape[1]), [float(v) for v in start[:n_parameters]],
        DecayFitConstraints(VectorInt32(codes)))

    n_rows = matrix.shape[0]
    parameters = np.asarray(batch.parameters).reshape(n_rows, n_parameters)
    n_reported = out.shape[1] - 1
    out[:, :n_reported] = parameters[:, :n_reported]
    out[:, n_reported] = np.asarray(batch.objective)
    return out


class _DecayFit23Compat(object):
    """Deprecated stand-in for the removed ``DecayFit23`` static-method class."""

    @staticmethod
    def modelf(param, irf, bg, dt, corrections, model):
        """Evaluate the model into ``model``, in place.

        Kept because there is no other way to obtain the *un-normalised* curve:
        ``DecayFit2.evaluate`` profiles the amplitude against the observed
        counts, which is what a fit wants and what a simulation does not.
        """
        _warn("tttrlib.DecayFit23.modelf", "tttrlib.decay_fit23_model_curve")
        decay_fit23_model_curve(param, irf, bg, dt, corrections, model)
        return model

    @staticmethod
    def fit_matrix(data, x0, fixed, bifl_scatter, p2s_flag, problem, out):
        """Fit every row of ``data`` into ``out`` as ``[tau, gamma, r0, rho, 2I*]``.

        fit23 kept its own signature because its two flags were separate
        arguments rather than slots in the parameter vector.
        """
        _warn("tttrlib.DecayFit23.fit_matrix", "tttrlib.DecayFit2.fit_many")
        return _batch_into("fit23", 4, data, x0, fixed, problem, out)


class _DecayFit24Compat(object):
    """Deprecated stand-in for the removed ``DecayFit24`` static-method class."""

    @staticmethod
    def fit_matrix(data, x0, fixed, problem, out):
        _warn("tttrlib.DecayFit24.fit_matrix", "tttrlib.DecayFit2.fit_many")
        return _batch_into("fit24", 5, data, x0, fixed, problem, out)


class _DecayFit25Compat(object):
    """Deprecated stand-in for the removed ``DecayFit25`` static-method class."""

    @staticmethod
    def fit_matrix(data, x0, fixed, problem, out):
        # x0 slot 5 is r0, which this model treats as its sixth parameter; the
        # old caller supplied it as a fixed "extra input" rather than a fit
        # parameter, and the shorter `fixed` mask holds it.
        _warn("tttrlib.DecayFit25.fit_matrix", "tttrlib.DecayFit2.fit_many")
        return _batch_into("fit25", 6, data, x0, fixed, problem, out)


class _DecayFit26Compat(object):
    """Deprecated stand-in for the removed ``DecayFit26`` static-method class."""

    @staticmethod
    def fit_matrix(data, x0, fixed, problem, out):
        _warn("tttrlib.DecayFit26.fit_matrix", "tttrlib.DecayFit2.fit_many")
        return _batch_into("fit26", 1, data, x0, fixed, problem, out)


#: The removed static-method class, under the name callers used.
DecayFit23 = _DecayFit23Compat
DecayFit24 = _DecayFit24Compat
DecayFit25 = _DecayFit25Compat
DecayFit26 = _DecayFit26Compat
