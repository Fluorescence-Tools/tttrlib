import numpy as np
import tttrlib


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
        self._m_param = tttrlib.CreateMParam(
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
