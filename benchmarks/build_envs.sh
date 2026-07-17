#!/usr/bin/env bash
# Build isolated benchmark venvs with uv. Each competitor gets its own env so the
# base tttrlib dev env is never touched. Logs go to benchmarks/logs/.
set -u
cd "$(dirname "$0")"
ROOT="$(pwd)"
VENVS="$ROOT/.venvs"
LOGS="$ROOT/logs"
mkdir -p "$VENVS" "$LOGS"

build () {
  local name="$1"; shift
  local py="$1"; shift
  echo "=== [$name] creating venv (py$py) ==="
  uv venv --python "$py" "$VENVS/$name" >"$LOGS/$name.log" 2>&1
  echo "=== [$name] installing: $* ==="
  VIRTUAL_ENV="$VENVS/$name" uv pip install "$@" >>"$LOGS/$name.log" 2>&1
  local rc=$?
  echo "=== [$name] done rc=$rc ==="
}

# 1. flimlib — C library, curve fitting (RLD, LMA, Bayes, phasor)
build flimlib 3.10 flimlib numpy scipy matplotlib

# 2. reading — ptufile (Gohlke) PTU reader
build read 3.10 ptufile numpy tifffile matplotlib

# 3. PyBroMo — Brownian-motion single-molecule simulator
build pybromo 3.10 "$ROOT/../junk/PyBroMo" numpy scipy tables matplotlib pandas numba

# 4. FRETBursts — burst analysis (has Cython ext)
build fretbursts 3.10 numpy scipy matplotlib pandas lmfit numba seaborn tables phconvert
VIRTUAL_ENV="$VENVS/fretbursts" uv pip install "$ROOT/../junk/FRETBursts" >>"$LOGS/fretbursts.log" 2>&1
echo "=== [fretbursts] repo install rc=$? ==="

# 5. FLIMKit — needs py>=3.12; FLIM/phasor fitting deps + MLX for the GPU backend
#    (mlx runs FLIMKit's per-pixel fit on the Apple-silicon GPU; get_backend picks
#    mlx>cuda>mps>rocm, so on other machines install torch or cupy instead).
build flimkit 3.12 numpy scipy matplotlib phasorpy ptufile numba tifffile sdtfile lz4 PyWavelets pandas openpyxl tqdm psutil xarray mlx

# 6. H2MM_C — reference pthreads C engine (Harris/Pirchi) for the H2MM comparison
build h2mm_c 3.10 "$ROOT/../../chisurf/junk/H2MM_C" numpy

# 7. H2MM numba — the ChiSurf numpy/numba engine the tttrlib C++ port derives from
build h2mm_numba 3.10 numpy numba

# 8. pycorrelate — reference NumPy photon-timestamp FCS correlator
build pycorrelate 3.10 pycorrelate numpy

echo "ALL ENV BUILDS ATTEMPTED"
