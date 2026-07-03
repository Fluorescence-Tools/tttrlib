.. _file_formats:

TTTR File Formats and Conversion
================================

tttrlib reads and writes all common time-tagged time-resolved (TTTR) container
formats. This page explains each supported file type, the read/write support
matrix, and what information survives a conversion between formats. All
conversions shown here are exercised by the unit tests
(``test/python/tttr/test_TTTR_roundtrip.py``) and the gallery example
:doc:`auto_examples/tttr/plot_tttr_file_conversion`.

Every TTTR file, regardless of vendor, decodes into the same four per-event
arrays:

- **macro times** — the coarse arrival time (sync counter for T3, time tag for T2),
- **micro times** — the TCSPC arrival time within the excitation period (T3 only),
- **routing channels** — the detector/router number,
- **event types** — photon (0) or marker (1); markers carry scanner
  synchronization in imaging data.

Round-trip fidelity in tttrlib is defined on these decoded arrays, not on the
raw byte stream: a written file may pack overflow records differently than the
original, but reading it back yields identical arrays.

Supported file types
--------------------

PicoQuant PTU (``.ptu``)
   The PicoQuant *unified* container. A tagged header (typed key/value tags)
   is followed by a stream of 32-bit records. The record layout depends on
   the instrument and mode, stored in the ``TTResultFormat_TTTRRecType`` tag:
   PicoHarp T2/T3, HydraHarp v1 T2/T3, HydraHarp v2 / TimeHarp 260 T2/T3, or
   MultiHarp / PicoHarp 330 generic T2/T3. T3 records store the sync counter
   (10 bit for HydraHarp), micro time (15 bit) and channel (6 bit); T2
   records store a 25-bit time tag and channel but **no micro time**.
   Imaging (CLSM) acquisitions store scanner markers as special records and
   the scan geometry in ``ImgHdr_*`` tags.

PicoQuant HT3 (``.ht3``)
   The older HydraHarp v1/v2 container: a fixed binary header (identity,
   measurement and display settings, per-channel input settings, TT-mode
   block, optional imaging block) followed by the same HydraHarp T3 records
   as PTU. tttrlib selects the record decoder from the ``Ident`` and
   ``FormatVersion`` header fields.

SF-compressed HT3 (``.ht3``)
   HT3 files converted with Suren Felekyan's HT3 compression (Seidel lab,
   Düsseldorf). The header is a regular HydraHarp v1 HT3 header and the
   photon/marker records are ordinary HydraHarp T3 records, but a *run* of
   macro time overflow records is collapsed into a single overflow record
   whose lowest 24 bits carry the number of additional overflows
   (advancing the sync counter by ``(1 + count) × 1024``). tttrlib
   auto-detects SF compression when reading HT3 files by inspecting the
   overflow record payloads (plain v1 overflow records have an all-zero
   payload, so detection is unambiguous), and can write SF files — set
   ``header.tttr_record_type = 14`` to compress a T3 dataset on write.

Becker & Hickl SPC-130/150/830 (``.spc``, container ``SPC-130``)
   A single 4-byte header word holding the macro time clock, then 32-bit
   records: 12-bit macro time, 4-bit routing, 12-bit inverted ADC (micro
   time), and flag bits. Macro time overflows are carried by dedicated
   overflow records (28-bit overflow count) or a per-record overflow bit.
   Markers (pixel/line/frame clock from a scanner) are encoded with the
   invalid+mark bits; an optional ``.set`` sidecar file with instrument
   settings is parsed automatically when present.

Becker & Hickl SPC-600/630, 256-channel mode (``.spc``, container ``SPC-600_256``)
   Headerless 32-bit records with 8-bit inverted ADC, 17-bit macro time and
   3-bit routing. Every macro time overflow accounts for 2\ :sup:`17` units.

Becker & Hickl SPC-600/630, 4096-channel mode (``.spc``, container ``SPC-600_4096``)
   Headerless **6-byte** records with 12-bit inverted ADC, 24-bit macro time
   and 8-bit inverted routing. Overflow records advance the clock by
   2\ :sup:`24` units each.

Photon-HDF5 (``.hdf5``, ``.h5``)
   The open, vendor-neutral HDF5-based format (`photon-hdf5.org
   <http://photon-hdf5.org/>`__). The decoded arrays are stored directly:
   ``/photon_data/timestamps`` (64 bit), ``detectors`` (8 bit) and
   ``nanotimes`` (16 bit), with resolutions in ``timestamps_specs`` /
   ``nanotimes_specs``. tttrlib writes spec-conformant v0.5 files with the
   mandatory ``/setup`` and ``/identity`` groups (see the *phconvert*
   reference implementation). Ideal as an archival and interchange format —
   nothing that tttrlib decodes is lost.

Carl Zeiss ConfoCor3 raw (``.raw``, container ``CZ-RAW``)
   FCS raw data from the ConfoCor3. A 128-byte header (measurement identity,
   channel number, macro time clock) is followed by 32-bit records that hold
   *macro time deltas* only. There are no micro times and no per-event
   channels — one file per detection channel.

SM (``.sm``, container ``SM``)
   The MFD single-molecule format: a big-endian tagged header followed by
   12-byte records of a 64-bit macro time and a 32-bit channel number, and a
   26-byte trailer. No micro times.

Support matrix
--------------

.. list-table::
   :header-rows: 1
   :widths: 18 14 6 6 12 10 8 26

   * - Container
     - Record types
     - Read
     - Write
     - Micro time
     - Channel
     - Markers
     - Header metadata (on write)
   * - ``PTU``
     - PH T2/T3, HH v1/v2 T2/T3, generic T2/T3
     - ✓
     - ✓
     - 15 bit (T3) / — (T2)
     - 6 bit (PH: 4 bit)
     - ✓
     - **Full** — every header tag is re-written (except wide-string and
       binary-blob tags)
   * - ``HT3``
     - HH v1/v2 T3, PH T3, SF-compressed
     - ✓
     - ✓
     - 15 bit
     - 6 bit
     - ✓
     - **Parsed fields** — identity, measurement/display settings,
       per-channel inputs, sync rate, imaging (CLSM) block
   * - ``SPC-130``
     - SPC-130/140/150/830
     - ✓
     - ✓
     - 12 bit
     - 4 bit
     - ✓
     - **Macro clock only** (4-byte header); ``.set`` sidecar settings are
       read but not written
   * - ``SPC-600_256``
     - SPC-600/630 (32 bit)
     - ✓
     - ✓
     - 8 bit
     - 3 bit
     - ✗
     - **None** (headerless format)
   * - ``SPC-600_4096``
     - SPC-600/630 (48 bit)
     - ✓
     - ✓
     - 12 bit
     - 8 bit
     - ✗
     - **None** (headerless format)
   * - ``PHOTON-HDF5``
     - decoded arrays
     - ✓
     - ✓
     - 16 bit
     - 8 bit
     - ✗
     - **Resolutions + setup** — timestamps/nanotimes units, TCSPC bin
       count, ``/setup`` fields; ``/identity`` is regenerated
   * - ``CZ-RAW``
     - ConfoCor3 raw
     - ✓
     - ✓
     - ✗
     - header only
     - ✗
     - **Measurement identity** — measure id, position/kinetic/repetition
       indices, channel, macro clock
   * - ``SM``
     - SM
     - ✓
     - ✓
     - ✗
     - 32 bit
     - ✗
     - **Header fields** — version, comment, column names/resolutions
       (channel labels are dropped)

Same-format round trips (``read → write → read``) reproduce the decoded event
stream exactly for every container. Writing v1 HydraHarp records (HT3 v1
files, old PTU files) is supported so v1 sources round-trip without record
upgrades.

Metadata preservation
---------------------

``TTTR.header`` holds all metadata parsed from the source file as typed tags.
On writing, each container writer re-emits as much of that metadata as the
target format can physically store:

- **PTU** is the richest vendor target: the writer serializes the *entire*
  tag list, so foreign metadata (e.g. tags of a source SPC or HT3 header)
  is carried along verbatim. Only ``tyWideString`` and ``tyBinaryBlob``
  tags are skipped (with a warning).
- **HT3** has a fixed binary header: all fields the reader parses are
  restored from tags (including the imaging block used by CLSM
  reconstruction). Fields that are not parsed into tags (display curve
  mappings, hardware module list) are zeroed.
- **Photon-HDF5** keeps the time calibrations in
  ``timestamps_specs``/``nanotimes_specs`` and preserves ``/setup`` values
  from a Photon-HDF5 source; other groups are regenerated.
- **SPC-130/SPC-600, CZ-RAW, SM** headers are small fixed structures — only
  the fields listed in the table survive.

The essential calibration — macro time resolution and micro time
resolution — is preserved by *every* writable container that has a header:
PTU and Photon-HDF5 store both explicitly, HT3 stores them as sync rate and
resolution (the writer derives the sync rate from the global resolution when
transcoding), SPC-130 and CZ-RAW store the macro clock. Only the headerless
SPC-600 containers cannot carry calibration; check
``header.macro_time_resolution`` after reading such files.

When metadata fidelity matters across a conversion, prefer **PTU** (vendor
tooling compatibility) or **Photon-HDF5** (open interchange) as the target.

Conversion between formats
--------------------------

To convert, read a file and write it with a header whose container (and
optionally record) type points at the target format:

.. code-block:: python

   import tttrlib

   data = tttrlib.TTTR("measurement.spc", "SPC-130")

   header = data.header
   header.tttr_container_type = 0   # PQ_PTU_CONTAINER
   header.tttr_record_type = 4      # PQ_RECORD_TYPE_HHT3v2
   data.write("measurement.ptu")

If the header's record type does not fit the target container, tttrlib
falls back to the container's canonical record type (HydraHarp v2 T3 for
PTU/HT3). Container and record type identifiers:

.. list-table::
   :header-rows: 1
   :widths: 30 10 40 10

   * - Container
     - ID
     - Record type
     - ID
   * - ``PTU``
     - 0
     - ``PQ_RECORD_TYPE_HHT2v2``
     - 1
   * - ``HT3``
     - 1
     - ``PQ_RECORD_TYPE_HHT3v1``
     - 3
   * - ``SPC-130``
     - 2
     - ``PQ_RECORD_TYPE_HHT3v2``
     - 4
   * - ``SPC-600_256``
     - 3
     - ``PQ_RECORD_TYPE_PHT3``
     - 5
   * - ``SPC-600_4096``
     - 4
     - ``BH_RECORD_TYPE_SPC130``
     - 7
   * - ``PHOTON-HDF5``
     - 5
     - ``BH_RECORD_TYPE_SPC600_256``
     - 8
   * - ``CZ-RAW``
     - 6
     - ``BH_RECORD_TYPE_SPC600_4096``
     - 9
   * - ``SM``
     - 7
     - ``CZ_RECORD_TYPE_CONFOCOR3`` / ``SM_RECORD_TYPE`` / generic T3/T2
     - 10 / 11 / 12 / 13
   * -
     -
     - ``PQ_RECORD_TYPE_SF_HT3`` (SF-compressed HT3)
     - 14

What survives a conversion
~~~~~~~~~~~~~~~~~~~~~~~~~~

Conversions are lossy when the target format cannot represent a field. The
rules follow directly from the record layouts above:

.. list-table::
   :header-rows: 1
   :widths: 30 70

   * - Conversion
     - Fidelity
   * - anything → ``PHOTON-HDF5``
     - **Lossless** for macro times, micro times and channels (event types
       are not stored; markers read back as photons).
   * - ``SPC-130`` → PTU/HT3 (T3)
     - Lossless — 12-bit micro times and 4-bit channels fit the HydraHarp
       T3 record.
   * - PTU/HT3 (T3) → ``SPC-130``
     - Micro times clip to 12 bit, channels to 4 bit. Scanner markers are
       written as BH marker records (marker bits in the routing field).
   * - T3 → T2 (``PTU``)
     - Micro times are dropped (T2 records have none); macro times,
       channels and markers survive.
   * - T2 → T3
     - Micro times read as 0 and are written as 0.
   * - anything → ``SPC-600_256``
     - Micro times clip to 8 bit, channels to 3 bit, markers dropped.
   * - anything → ``SPC-600_4096``
     - Micro times clip to 12 bit, markers dropped.
   * - anything → ``SM``
     - Macro times and channels survive; micro times and markers are
       dropped.
   * - anything → ``CZ-RAW``
     - Only macro times survive (stored as 32-bit deltas). The channel
       number is a single header field (taken from the ``channel`` tag,
       default 1).

Time calibration across formats
~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~

Conversion preserves the *raw counter values*. The macro and micro time
resolutions travel in the header (``MeasDesc_GlobalResolution`` /
``MeasDesc_Resolution`` tags, or the format's native equivalent), so times
in seconds are preserved whenever the target header format can store the
calibration. The headerless SPC-600 containers assume a fixed clock; verify
``header.macro_time_resolution`` after reading converted files.

Tested conversion scripts
-------------------------

Ready-to-run conversion scripts (executed as part of the documentation
build and mirrored by unit tests):

- :doc:`auto_examples/tttr/plot_tttr_file_conversion` —
  the conversion matrix: PTU ⇄ SPC, → Photon-HDF5, → SM, T3 → T2.
- :doc:`auto_examples/beginner/plot_04_writing_files` —
  writing subsets and format copies.
- :doc:`auto_examples/tttr/plot_tttr_transcode` —
  transcoding with a header borrowed from another file.
