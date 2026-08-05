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

.. seealso::

   :doc:`formats/index` -- one page per container: what it is, how it is laid
   out, which record encodings it carries, and where to find a real file of it.

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
   settings is parsed automatically when present and re-emitted on write
   (see :ref:`bh_set_sidecar`).

Becker & Hickl SPC-QC (``.spc``, container ``SPC-QC``)
   The QC generation (SPC-QC-104/004 and SPC-QC-106/006) writes ``.spc`` files
   too, but with a record layout of its own. The lower 28 bits are common to
   every event kind: a 12-bit macro time (bits 0-11), a 4-bit routing signal
   (bits 12-15) and a 12-bit ADC (bits 16-27). Unlike **every** classic SPC
   card the ADC value is *not* inverted — ``0x000`` is 0 ns, the micro time the
   way SPCM histograms it. The top bits select the event:

   .. list-table::
      :header-rows: 1
      :widths: 22 26 52

      * - Event
        - QC-x04 (bits 31-30)
        - QC-x06 (bits 31-28)
      * - photon
        - ``00``, channel in bits 29-28
        - ``0xxx``, channel in bits 30-28
      * - macro time overflow
        - ``10``, all other bits zero
        - ``1000``, all other bits zero
      * - marker
        - ``01``, type in bits 15-12
        - ``1010``, type in bits 15-12
      * - GAP
        - ``11``, otherwise a photon
        - ``11xx``, otherwise a photon

   A macro time overflow is therefore the bare word ``0x80000000`` on both
   layouts and always stands for exactly one wrap of 4096 units — there is no
   count field, so idle stretches are not compressed. Writing an SPC-QC
   measurement back out reproduces the instrument's record stream byte for
   byte, so other readers see exactly what the module produced. A GAP record *is* a
   photon; it only warns that a FIFO overrun may precede it.

   A QC detector is identified by the router signal **and** the module input,
   so tttrlib keeps both in ``routing_channels``: the routing signal in the low
   bits and the input channel directly above them. Becker & Hickl reserve four
   routing bits for this whether or not a router is attached
   (``MeasFCSInfo.chan``, "bits 6-4 = input channel for QC-x0x modules", so a
   plain three-input measurement is chan 0, 16, 32 in the ``.sdt``). tttrlib
   instead places the input channel on the routing width the header declares,
   which keeps the numbering dense: with no router the channel is simply the
   module input — 0, 1, 2 — and with one, both dimensions still pack
   losslessly. The declared width is checked against the records first, and the
   full four bits are kept if any photon routes beyond it, so a wrong header
   cannot silently merge two detectors.

   The 4-byte header word carries flags where the classic one has reserved
   bits: number of routing bits (30-27), raw (26, always set), markers (25,
   imaging mode), femto (24), six-input-channel module (23) and the macro time
   clock in bits 21-0. The femto flag is what lets the QC clock be expressed at
   all — 2.048131 ns in the reference data needs femtoseconds, where the
   classic 0.1 ns unit would round it to 2.0 ns. Bit 23 selects the QC-x06
   record layout, whose wider channel field reaches into bit 30 and so cannot
   share a decoder with QC-x04.

   The QC modules run their TAC independently of the macro time clock, so the
   micro time resolution is not derivable from the header. tttrlib assumes
   the default TAC range SPCM writes (65.54 ns over 4096 channels, i.e. 16 ps
   per channel) and replaces it with ``SP_TAC_R``/``SP_ADC_RE`` as soon as a
   ``.set`` sidecar is found (see :ref:`bh_set_sidecar`).

   The header stores the macro time clock as a whole number of femtoseconds,
   so its precision is limited to one part in 4 × 10\ :sup:`6` (one LSB on the
   reference module's 2048131 fs). Against SPCM's own intensity trace the
   residual is about 4 × 10\ :sup:`-8` — a drift of a few microseconds per
   100 s, i.e. more than ten times *finer* than the header can express. Raw
   macro time counter values are exact; only their conversion to seconds
   inherits this limit, which matters solely for absolute timing over long
   acquisitions.

   .. note::

      The QC "absolute time" FIFO mode is not supported. There the micro time
      field instead holds the low 9 bits of a 4 ps absolute time
      (``t = (macrotime × 512 + microtime) × 4 ps``). Nothing in the ``.spc``
      header distinguishes it from the regular stream, so it cannot be detected
      from the file alone and would be decoded as ordinary micro times.

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
     - **Macro clock** (4-byte header) plus a companion ``.set`` sidecar file
       (written next to the ``.spc``) carrying the imaging geometry — see
       :ref:`bh_set_sidecar`
   * - ``SPC-QC``
     - QC-x04, QC-x06
     - ✓
     - ✓
     - 12 bit
     - 4 bit routing + 2 bit input (x04) / 3 bit (x06)
     - ✓
     - **Macro clock** (4-byte header, in fs) plus routing width and marker
       flag, and a companion ``.set`` sidecar — the sidecar is the only source
       of the micro time resolution
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
  the fields listed in the table survive. SPC-130 imaging data is the
  exception: its instrument setup and scan geometry are preserved in the
  companion ``.set`` sidecar (see :ref:`bh_set_sidecar`).

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

Transcoding is just *read one format, write another*. ``TTTR.write`` selects
the output container from the filename extension, so the common case needs no
extra arguments:

.. code-block:: python

   import tttrlib

   data = tttrlib.TTTR("measurement.spc", "SPC-130")

   data.write("measurement.ptu")     # → PicoQuant PTU  (inferred from .ptu)
   data.write("measurement.hdf5")    # → Photon-HDF5    (inferred from .h5/.hdf5)
   data.write("measurement.ht3")     # → HydraHarp HT3  (inferred from .ht3)

Recognised extensions are ``.ptu``, ``.ht3``, ``.spc``, ``.hdf5``/``.h5``,
``.raw`` and ``.sm``. Because all Becker & Hickl flavours share the ``.spc``
extension, writing an SPC source to ``.spc`` keeps its specific flavour (an
``SPC-600_256`` or ``SPC-QC`` file is not silently downgraded to ``SPC-130``);
only a cross-family target (e.g. a PTU source written to ``.spc``) switches
container.

**Forcing the format.** When the extension is absent or unusual, pass the
target explicitly — by container name or by numeric id:

.. code-block:: python

   data.write("measurement.dat", "PTU")     # by container name
   data.write("measurement.dat", None, 0)   # by container id (PQ_PTU_CONTAINER)

An explicit argument always wins over the extension. The older style of
setting ``header.tttr_container_type`` (and ``header.tttr_record_type`` to
choose a specific record encoding such as T2 vs T3) before ``write`` still
works and is the way to select the *record* type:

.. code-block:: python

   header = data.header
   header.tttr_container_type = 0   # PQ_PTU_CONTAINER
   header.tttr_record_type = 4      # PQ_RECORD_TYPE_HHT3v2
   data.write("measurement.ptu")

If the header's record type does not fit the target container, tttrlib
falls back to the container's canonical record type (HydraHarp v2 T3 for
PTU/HT3). tttrlib also fills in any mandatory metadata the target format
needs but the (possibly transcoded or freshly built) header lacks — macro
and micro time resolution, the micro-time channel count, and, for PTU, the
records-per-file count and bits-per-record — without overwriting values that
came from the source. This keeps written files valid regardless of the
source container, including files assembled from bare arrays with
``append_events``. Container and record type identifiers:

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
   * - ``PHOTONS``
     - 8
     - ``PQ_RECORD_TYPE_SF_HT3`` (SF-compressed HT3)
     - 14
   * - ``SPC-QC``
     - 9
     - ``BH_RECORD_TYPE_SPCQC_X04`` / ``_X06``
     - 15 / 16

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
   * - anything → ``SPC-QC``
     - Micro times clip to 12 bit; the channel splits into a 4-bit router
       signal (bits 3-0) and an input channel (bits 6-4, 2 bit on QC-x04 and
       3 on QC-x06), so channels 0..63 survive (0..127 on QC-x06) and wider
       ones clip. Markers survive as marker records. The overflow record carries no count, so one
       word per 4096 macro time units of idle time is emitted — long, sparse
       measurements produce large files (as they do on the instrument).
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

.. _bh_set_sidecar:

Becker & Hickl ``.set`` sidecar and imaging round-trip
------------------------------------------------------

A Becker & Hickl ``.spc`` file carries only photon and marker records; the
CLSM imaging geometry and the full instrument setup live in a companion
``.set`` file with the same base name. tttrlib treats the two as a unit:

- **Reading** a ``.spc`` automatically parses a neighbouring ``.set`` (same
  base name). The imaging geometry becomes ``ImgHdr_PixX`` / ``ImgHdr_PixY``
  tags, and the *entire* ``.set`` — which is largely binary — is preserved
  verbatim inside the header (base64-encoded in the ``BH_SPC_SetFile`` tag).
- **Writing** a ``.spc`` emits a companion ``.set`` next to it. When the
  header still holds a preserved ``.set`` (read directly, or carried through
  another container), it is restored **byte-for-byte**; otherwise a minimal
  ``.set`` is synthesised from the imaging tags.

Because the preserved ``.set`` rides along in the header, and a PTU header
stores arbitrary tags, the full setup survives a detour through PTU. The
frame and line markers are ordinary marker events, preserved by the record
writers, so a scanned image transcoded to PTU and back is identical:

.. code-block:: python

   import tttrlib

   # .spc + .set  →  .ptu   (the .set travels inside the PTU header)
   img = tttrlib.TTTR("scan.spc", "SPC-130")
   img.write("scan.ptu")

   # .ptu  →  .spc + .set   (the original .set is written back byte-for-byte)
   tttrlib.TTTR("scan.ptu").write("roundtrip.spc")

**Opening the converted image.** A BH SPC image transcoded to PTU keeps its
markers, so it reconstructs *exactly* with the Becker & Hickl reading
routine. On read, tttrlib tags such files with a reading-routine hint
(``BH_SPC_ReadingRoutine``) that survives the conversion, so the PTU opens as
a CLSM image with no extra arguments:

.. code-block:: python

   clsm = tttrlib.CLSMImage(filename="scan.ptu")   # auto-selects BH_SPC130
   clsm.intensity.shape                             # (frames, lines, pixels)

The hint only applies when the caller does not pass an explicit
``reading_routine``; pass one to override it.

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
