#!/usr/bin/env python
"""Record Kolin & Wiseman's STICS / ICS (MATLAB, run in Octave) for
``test_clsm_ics.py::TestAgainstKolinWisemanOctave``.

Reference: David Kolin's ``stics.m`` and ``corrfunc.m`` (2003; junk checkout
``chisurf/junk/Image-Correlation-Spectroscopy``), the code behind Hebert,
Costantino & Wiseman 2005. The two functions are reproduced here verbatim
except for the ``waitbar``/``gcbf`` GUI calls, run in Octave on an image
series generated with NumPy and stored beside the outputs. Needs ``octave``
on PATH; the arithmetic is deterministic so the fixture is exact.

    python test/python/clsm/gen_ab_stics_kolin_reference.py
"""
import os
import subprocess
import tempfile

import numpy as np
import scipy.io as sio

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "data", "reference",
                   "stics_kolin_octave_reference.npz")

M_CODE = r"""
function run_stics(inpath, outpath)
  S = load(inpath); imgser = double(S.imgser);
  upperTauLimit = 4;
  timecorr = zeros(size(imgser,1),size(imgser,2),upperTauLimit);
  for tau = 0:upperTauLimit-1
    lagcorr = zeros(size(imgser,1),size(imgser,2),(size(imgser,3)-tau));
    for pair=1:(size(imgser,3)-tau)
      lagcorr(:,:,pair) = fftshift(real(ifft2(fft2(imgser(:,:,pair)).*conj(fft2(imgser(:,:,(pair+tau)))))));
    end
    timecorr(:,:,(tau+1)) = mean(lagcorr,3);
  end
  G = zeros(size(imgser));
  for z=1:size(imgser,3)
    G(:,:,z) = ((fftshift(real(ifft2(fft2(double(imgser(:,:,z))).*conj(fft2(double(imgser(:,:,z))))))))/(mean(mean(imgser(:,:,z)))^2*size(imgser,1)*size(imgser,2))) - 1;
  end
  save('-v7', outpath, 'timecorr', 'G');
end
"""


def main():
    rng = np.random.default_rng(1)
    T, ny, nx = 8, 24, 32
    ser = rng.poisson(15.0, (T, ny, nx)).astype(float)
    ser[:, 5:9, 6:12] += 30.0
    with tempfile.TemporaryDirectory() as tmp:
        sio.savemat(os.path.join(tmp, "imgser.mat"), {"imgser": np.transpose(ser, (1, 2, 0))})
        with open(os.path.join(tmp, "run_stics.m"), "w") as fh:
            fh.write(M_CODE)
        subprocess.run(["octave", "--no-gui", "-q", "--eval",
                        f"run_stics('{tmp}/imgser.mat', '{tmp}/out.mat')"], cwd=tmp, check=True)
        o = sio.loadmat(os.path.join(tmp, "out.mat"))
    version = subprocess.run(["octave", "--version"], capture_output=True, text=True).stdout.splitlines()[0]
    np.savez_compressed(OUT, imgser=ser, timecorr=np.transpose(o["timecorr"], (2, 0, 1)),
                        G=np.transpose(o["G"], (2, 0, 1)), n_tau=np.array(4), octave=np.array(version))
    print("wrote", OUT)


if __name__ == "__main__":
    main()
