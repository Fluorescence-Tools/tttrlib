# tttrlib ImageJ / Fiji plugin

A minimal ImageJ plugin that opens a time-tagged time-resolved confocal (CLSM)
file with tttrlib and shows the reconstructed intensity image as a stack
(one slice per frame). Menu: **Plugins ▸ tttrlib ▸ Open TTTR CLSM Image**.

## Contents
- `TTTR_CLSM_Reader.java` — the plugin (`ij.plugin.PlugIn`).
- `plugins.config` — the ImageJ menu entry, bundled into the JAR.

## Prerequisites
1. The tttrlib Java bindings (SWIG proxies + JNI native), built via CMake:
   ```sh
   cmake -S . -B build-java -DBUILD_PYTHON_INTERFACE=OFF -DBUILD_JAVA_INTERFACE=ON -DBUILD_LIBRARY=ON
   cmake --build build-java --target tttrlibJava
   ```
   This produces the proxy `.java` files under `build-java/java-pkg/...` and the
   native `libtttrlibjni.{so,dylib,jnilib}`/`tttrlibjni.dll`.
2. `ij.jar` from an ImageJ/Fiji installation (e.g. `Fiji.app/jars/ij-*.jar`).

## Build the plugin JAR
```sh
SRC=build-java/java-pkg/src/main/java
NATIVE=$(find build-java -name 'libtttrlibjni*' -o -name 'tttrlibjni.dll' | head -1)

# 1. compile the SWIG proxies + NativeLoader + the plugin against ij.jar
javac -cp ij.jar -d classes \
  $SRC/io/github/fluorescencetools/tttrlib/*.java \
  ext/java/pkg/src/main/java/io/github/fluorescencetools/tttrlib/NativeLoader.java \
  ext/java/imagej/TTTR_CLSM_Reader.java

# 2. bundle classes + the menu config + the native library into one JAR
mkdir -p jarroot/native/$(uname -s)
cp -r classes/* jarroot/
cp ext/java/imagej/plugins.config jarroot/
cp "$NATIVE" jarroot/          # or into resources/native/<platform>/ for NativeLoader
jar cf tttrlib_imagej.jar -C jarroot .
```

## Install
Copy `tttrlib_imagej.jar` into `ImageJ/plugins/` (or `Fiji.app/plugins/`) and
make the native library discoverable — either put it next to the JAR and start
ImageJ with `-Djava.library.path=<dir>`, or rely on `NativeLoader`, which
extracts a bundled `native/<os-arch>/libtttrlibjni.*` from the JAR at load time.

Restart ImageJ; the command appears under **Plugins ▸ tttrlib**.

## Notes
- The intensity image is reconstructed for a single routing channel (chosen in
  the dialog). Multi-channel splitting and lifetime images are future additions.
- Reconstruction relies on scan markers in the file; files without CLSM markers
  will report that no image could be built.

## CLSM auto-configuration
Correct reconstruction of PicoQuant PTU/HT3 images needs CLSM markers and pixel
dimensions read from the file header (`ImgHdr_LineStart`, `ImgHdr_LineStop`,
`ImgHdr_Frame`, `ImgHdr_PixX`, `ImgHdr_PixY`). That logic used to live only in the
Python wrapper (`CLSMImage.read_clsm_settings`), so Java/R/native C++ produced a
wrong geometry with the raw defaults. It is now ported into the C++ `CLSMImage`
constructor (`src/CLSMImage.cpp`), so this plugin reconstructs the same image as
the Python API (verified: 40x256x256, sum 3364714 for the reference HT3 file).
Passing explicit markers/dimensions still overrides the header-derived values.
