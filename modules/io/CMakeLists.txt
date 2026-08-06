# SPDX-License-Identifier: BSD-3-Clause
#
# The I/O layer. Everything here is "a thing that reads or writes a file";
# nothing here knows what a photon stream is used for. See README.md.
#
# `base` is the format-agnostic half -- the registry, the header/tag model, the
# raw file access -- and every vendor module below depends on it and on nothing
# else. Declaration order is dependency order, so base comes first.
add_subdirectory(base)    # registry, headers/tags, raw file access

add_subdirectory(pq)      # PicoQuant PTU, HT3 (incl. _sf macro-time compression)
add_subdirectory(bh)      # Becker & Hickl SPC-130, SPC-600, SPC-QC, .set sidecar
add_subdirectory(cz)      # Zeiss ConfoCor3 raw
add_subdirectory(sm)      # Single-molecule .sm
add_subdirectory(ps)      # Photonscore LINCam .photons
add_subdirectory(be)      # BrightEyes-TTM .ttr
add_subdirectory(fl)      # FLIM LABS time tagger .bin (STT1, ITT1)
add_subdirectory(hdf5)    # Photon-HDF5 v0.5 -- decoded arrays, not a record encoding
add_subdirectory(image)   # TIFF arrays -- file I/O, but not a TTTR container
add_subdirectory(csv)     # CSV -> DataStore. Depends on core; see its CMakeLists.
add_subdirectory(hdf5_table)  # columnar HDF5 -> DataStore. Depends on core, so it
                              # cannot live in hdf5/, which core itself depends on.
add_subdirectory(store)   # the native .dstore file. No external dependency at all.
add_subdirectory(pto)     # PTO, the PhoTon cOntainer: EBML, DocType "pto".
