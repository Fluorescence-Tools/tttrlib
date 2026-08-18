# `io/base` — Base File I/O Framework

`io/base` provides the format-agnostic infrastructure for reading and writing TTTR files.

## Contents

- **`TTTRFormat.h` / `IORegistry.cpp`**: Registry and detection of supported TTTR file formats.
- **`FileIO.h` / `FileIO.cpp`**: Low-level platform file access utilities.
- **`TTTRTags.h` / `TTTRTags.cpp`**: Metadata tag structures and container attribute mappings.
- **`TTTRHeaderTypes.h`**: the header's value types and the container-type ids.
- **`TTTRRecordTypes.h`**: the record encodings (`record_type_name`,
  `record_bytes`, which are decodable) every format module names.
- **`PhotonSink.h` / `PhotonSink.cpp`**: where decoded photons go — the
  interface a reader pushes into, so a decoder does not have to own a `TTTR`.
- **`TTTRStreamWriter.h` / `TTTRStreamWriter.cpp`**: writing a photon stream
  out in a container's encoding, incrementally.

## Dependencies

- Depends on `util`.
