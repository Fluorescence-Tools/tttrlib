"""

fit24
^^^^
Fit24 optimizes is a bi-exponential model function with two fluorescence lifetimes
:math:`\tau_1`, :math:`\tau_2`, and amplitude of the second lifetime :math:`a_2`,
the fraction scattered light :math:`\gamma`, and a constant offset to experimental
data. Fit24 does not describe anisotropy. The decays passed to the fit in the
:term:`Jordi` format. The two decays in the Jordi array are both treated by the
same model function and convoluted with the corresponding
:term:`instrument response function<IRF>`. The model function is

.. :math:

    M_i =(1-a_2) \exp(i\Delta t/\tau_1) + a_2 \exp(i\Delta t/\tau_2)

where :math:`\Delta t` is the time per micro time channel, :math:`i` is the micro time
channel number, :math:`a_2` is the fraction of the second species. The model function
is corrected for the fraction of background and a constant offset.

.. :math:

    g_i =(1 - \gamma) \frac{M_i}{\sum_i M_i} + \gamma \frac{B_i}{\sum_i B_i} + c

Where,  :math:`c` is a constant offset, :math:`B` the background pattern, :math:`M`
the model function and :math:`\gamma` the fraction of scattered light.


"""