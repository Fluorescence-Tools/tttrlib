# tttrlib ImageJ / Fiji plugin

> **Maturity note.** The Java binding (and this plugin) share the tested C++ core
> with Python, but the Java layer and plugin are **newer and less tested** than
> the Python package. Please [report issues](https://github.com/fluorescence-tools/tttrlib/issues).
> We are working to mirror the Python test suite in Java (and R).

An ImageJ/Fiji plugin that opens time-tagged time-resolved confocal (CLSM) files
— PicoQuant PTU/HT3, Becker &amp; Hickl SPC, Leica SP5/SP8 — with the tttrlib C++
engine (via JNI) and reconstructs, per routing channel:

* an **Intensity** image stack (photon counts, one slice per frame),
* a **FastLifetime** image stack (mean photon arrival time per pixel),
* a **Phasor** image (g and s coordinates per pixel),
* **Number & Brightness (N&B)** maps (per-pixel B, N and epsilon), and
* a **Decay** plot (aggregate micro-time histogram per group, exportable as text).

A **Stack frames** option collapses the frame dimension to a single frame
(intensity summed; FastLifetime/Phasor accumulated in tttrlib). N&B always uses
the per-frame data (it is a fluctuation measure) regardless of that option.

The **Auto-correct IRF offset** option estimates the instrument-response offset
from the rise of the aggregate decay (the leading edge of the micro-time
histogram) and applies it to both FastLifetime (subtracts the offset) and Phasor
(rotates each pixel phasor by the corresponding phase), so no separate IRF file
is needed.

It follows the conventions of a standard Fiji FLIM reader plugin, but reads every
container format tttrlib supports and reconstructs directly with the C++ engine.

Menu: **Plugins ▸ tttrlib ▸ Open TTTR CLSM Image**.

![example output](img/clsm_example.png)

Source: [`ext/java/imagej/`](../ext/java/imagej).

## Channel groups

The **Channel groups** dialog field groups routing channels into colour channels
with the syntax `groupA;groupB;...`, each group a comma-separated channel list:

| Input | Result |
|---|---|
| `0` | single channel from routing channel 0 |
| `1,3;2,4` | colour ch 1 = routing {1,3} combined; colour ch 2 = routing {2,4} |

Every requested image type (Intensity, FastLifetime, Phasor) is produced as a
composite (multi-colour) hyperstack with one channel per group. Phasor is output
as two composites (`g` and `s`).

## PIE / micro-time ranges

The **Micro-time ranges** dialog field gates photons by arrival time with the
syntax `start,stop;start,stop;...` — e.g. `0,111;200,499;900,1200`. Each pair is
an inclusive micro-time window (prompt, delayed, …); the plugin reconstructs a
separate image per (channel group × window), labelled `chG [start-stop]`. Leave
the field empty for no gating. Combine with channel groups for PIE/ALEX-style
donor/acceptor prompt-and-delayed splitting.

## Decay export

Tick **Decay** to plot the aggregate micro-time histogram (one curve per channel
group) in an ImageJ Plot window. Use it to locate the prompt/delayed windows for
the micro-time-range field; the Plot's **Data ▸ Save Data…** exports the curves
as text (CSV). The x-axis is micro time in nanoseconds (channel × resolution).

## Number & Brightness (N&B)

Tick **Number & Brightness (N&B)** to compute, per pixel and per channel-group,
the moments of the intensity across frames (following the chisurf `nb_maps`
convention):

- `mean` = ⟨k⟩, `variance` = population variance over frames (ddof = 0),
- **B = variance / mean** (apparent brightness),
- **N = mean² / variance** (apparent number),
- **epsilon = B − 1** (true molecular brightness).

The plugin opens `B`, `N` and `epsilon` composites (one channel per group). N&B
requires ≥ 2 frames and is unaffected by the *Stack frames* option.

## Decay from a mask (Plugins ▸ tttrlib ▸ Decay from Mask)

A second command computes the decay of a **selected region**, one column per
routing channel:

1. Open a file with *Open TTTR CLSM Image* (the resulting images are tagged with
   their source file).
2. Draw a selection (ROI) on the image — any shape; no selection = whole field of
   view.
3. Run **Plugins ▸ tttrlib ▸ Decay from Mask**.

The photons inside the masked pixels are histogrammed per routing channel and
written to a **Results table**: a `time_ns` column plus one column per channel
(`ch0`, `ch1`, … — routing channel 0 in column 0, channel 1 in column 1, and so
on). Save it with **File ▸ Save As** (CSV). The curves are also shown as a Plot.
Channels are auto-detected from the data (override with the *Channels* field);
*Micro-time coarsening* rebins the decay.

## Installation

The plugin ships as a **single, self-contained JAR** — `tttrlib_imagej-<version>.jar`.
It bundles the JNI native library for **all common platforms** at once:

| Platform            | Bundled native                          |
|---------------------|-----------------------------------------|
| Linux x86-64        | `native/linux-x86-64/libtttrlibjni.so`  |
| macOS Intel         | `native/darwin-x86-64/libtttrlibjni.dylib` |
| macOS Apple silicon | `native/darwin-aarch64/libtttrlibjni.dylib` |
| Windows x86-64      | `native/win32-x86-64/tttrlibjni.dll`    |

At runtime `NativeLoader` extracts and loads the one matching your OS/CPU, so the
same JAR works everywhere and **no `java.library.path` setup is needed** — you
just drop it into ImageJ/Fiji.

### 1. Download the JAR

- **Released version (recommended):** the
  [**Releases page**](https://github.com/fluorescence-tools/tttrlib/releases) —
  download `tttrlib_imagej-<version>.jar` from the latest release's *Assets*.
- **Latest development build:** the
  [**Actions**](https://github.com/fluorescence-tools/tttrlib/actions) tab →
  open the most recent successful **CI** run on `main`/`development` → scroll to
  **Artifacts** → download **`tttrlib-imagej-plugin`** (a zip containing the JAR).

### 2. Install into ImageJ / Fiji

**Fiji (easiest):** drag the JAR onto the Fiji main window and confirm — Fiji
copies it into `plugins/` for you. Then **Help ▸ Refresh Menus** (or restart).

**Manual (Fiji or plain ImageJ):** copy the JAR into the `plugins/` folder of
your installation and restart:

| Application | plugins folder |
|-------------|----------------|
| Fiji (Linux/Windows) | `Fiji.app/plugins/` |
| Fiji (macOS) | `Fiji.app/plugins/` — `Fiji.app` is a normal folder; open it directly (do **not** use *Show Package Contents*) |
| ImageJ | `ImageJ/plugins/` |

After restart the commands appear under **Plugins ▸ tttrlib** (*Open TTTR CLSM
Image* and *Decay from Mask*).

Requirements: ImageJ/Fiji running on **Java 8 or newer** (Fiji bundles its own
Java, so this is satisfied out of the box). Only one copy of the JAR should be in
`plugins/` — delete older `tttrlib_imagej-*.jar` versions when updating.

### Build from source
Build the Java bindings (SWIG proxies + JNI native) with CMake, then assemble
the plugin JAR:

```sh
# 1. tttrlib Java bindings
cmake -S . -B build-java -DBUILD_PYTHON_INTERFACE=OFF -DBUILD_JAVA_INTERFACE=ON -DBUILD_LIBRARY=ON
cmake --build build-java --target tttrlibJava      # -> proxies + libtttrlibjni.*

# 2. plugin JAR (needs ij.jar from a Fiji install). Name the native as NativeLoader
#    expects: libtttrlibjni.so / .dylib / tttrlibjni.dll under native/<os-arch>/.
SRC=build-java/java-pkg/src/main/java
NATIVE=$(find build-java -name 'libtttrlibjni*' -o -name 'tttrlibjni.dll' | head -1)
mkdir -p jarroot/native/$(uname -s | tr 'A-Z' 'a-z')-$(uname -m)
javac -cp ij.jar -d jarroot \
  $SRC/io/github/fluorescencetools/tttrlib/*.java \
  ext/java/pkg/src/main/java/io/github/fluorescencetools/tttrlib/NativeLoader.java \
  ext/java/imagej/src/main/java/io/github/fluorescencetools/tttrlib/imagej/*.java
cp ext/java/imagej/src/main/resources/plugins.config jarroot/
cp "$NATIVE" jarroot/native/$(uname -s | tr 'A-Z' 'a-z')-$(uname -m)/
jar cf tttrlib_imagej.jar -C jarroot .
```

This produces a single-platform JAR (only your OS's native). CI's
`imagej_native` + `imagej_assemble` jobs build the native on Linux, macOS
(Intel + Apple silicon) and Windows and merge them into the one cross-platform
JAR published on the Releases page and as the `tttrlib-imagej-plugin` artifact.

### Build from source
Build the Java bindings (SWIG proxies + JNI native) with CMake, then assemble
the plugin JAR:

```sh
# 1. tttrlib Java bindings
cmake -S . -B build-java -DBUILD_PYTHON_INTERFACE=OFF -DBUILD_JAVA_INTERFACE=ON -DBUILD_LIBRARY=ON
cmake --build build-java --target tttrlibJava      # -> proxies + libtttrlibjni.*

# 2. plugin JAR (needs ij.jar from a Fiji install)
SRC=build-java/java-pkg/src/main/java
NATIVE=$(find build-java -name 'libtttrlibjni*' -o -name 'tttrlibjni.dll' | head -1)
mkdir -p jarroot/native/$(uname -s | tr 'A-Z' 'a-z')-$(uname -m)
javac -cp ij.jar -d jarroot \
  $SRC/io/github/fluorescencetools/tttrlib/*.java \
  ext/java/pkg/src/main/java/io/github/fluorescencetools/tttrlib/NativeLoader.java \
  ext/java/imagej/src/main/java/io/github/fluorescencetools/tttrlib/imagej/*.java
cp ext/java/imagej/src/main/resources/plugins.config jarroot/
cp "$NATIVE" jarroot/native/$(uname -s | tr 'A-Z' 'a-z')-$(uname -m)/
jar cf tttrlib_imagej.jar -C jarroot .
```

A `pom.xml` (scijava parent) is provided in `ext/java/imagej/` for IDE /
Fiji-ecosystem builds; CI stages the generated proxies and native into it before
`mvn package`.

## Usage
1. **Plugins ▸ tttrlib ▸ Open TTTR CLSM Image**, choose a file.
2. In the dialog pick the routing channel and which images to build
   (Intensity, FastLifetime, IRF auto-correction, minimum photons per pixel).
3. The reconstructed stacks open as ImageJ windows.

Scan markers and pixel dimensions are read from the file header automatically
(PicoQuant `ImgHdr_*` tags, Becker &amp; Hickl set-file metadata).

## Benchmark

`test/java/Benchmark.java` times tttrlib head-less; `ext/java/imagej/benchmark.ijm`
times the reader inside Fiji (and can be extended to compare against any other
installed reader).

Representative tttrlib throughput (Apple M-series, single thread, best of 5):

| File | Photons | Read | Throughput | Reconstruct (intensity) |
|---|---|---|---|---|
| `pq_ht3_clsm.ht3` | 15.6 M | 99 ms | ~158 Mphotons/s | 40×256×256 in 98 ms |

The photon stream is decoded in a single vectorized C++ pass and reconstructed
with multithreaded kernels — typically several times faster than pure-Java
readers that decode records one at a time.

## Limitations
* **Java array output**: intensity/FastLifetime are transferred via preallocated
  primitive arrays (`get_intensity_into`, `get_mean_micro_time_into`); general
  N-dimensional output-array marshalling is a follow-up.
* **Single-frame FLIM PTU**: files that carry no frame marker (a single FLIM
  frame) currently reconstruct to 0 frames — a tttrlib CLSM reconstruction gap,
  tracked separately. Framed PTU/HT3 files work.
