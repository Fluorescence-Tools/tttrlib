


Writing TTTR-files
==================
TTTR objects can be writen to files using the method ``write`` of TTTR objects.

.. code-block:: python

    import tttrlib
    data = tttrlib.TTTR('./data/bh/bh_spc132.spc', 'SPC-130')
    data_sliced = data[:10]
    output = {
        'filename': 'sliced_data.spc'
    }
    data_sliced.write(**output)

This way, as shown above, data can be sliced into pieces or converted into other
data types.

A TTTR object that was created for instance from a SPC file can be saved as PTU
file. For that the header information from a PTU file need to be provided when
writing to a file.


.. code-block:: python

    import tttrlib
    import json
    data = tttrlib.TTTR('./data/bh/bh_spc132.spc', 'SPC-130')
    header = data.header
    header.tttr_container_type = 0 # PTU
    header.tttr_record_type = 4 # PQ_RECORD_TYPE_HHT3v2
    header_dict = {
        "tags": [
            {"name": "MeasDesc_BinningFactor",
             "idx": -1,
             "type": 268435464,
             "value": 1
             },
            {"name": "TTResultFormat_BitsPerRecord",
             "idx": -1,
             "type": 268435464,
             "value": 1
             },
            {
                "idx": -1,
                "name": "MeasDesc_Resolution",
                "type": 536870920,
                "value": 3.2958984375e-12
            },
            {
                "idx": -1,
                "name": "MeasDesc_GlobalResolution",
                "type": 536870920,
                "value": 1.35e-08
            },
            {
                "idx": -1,
                "name": "TTResultFormat_BitsPerRecord",
                "type": 268435464,
                "value": 32
            },
            {
                "idx": -1,
                "name": "TTResultFormat_TTTRRecType",
                "type": 268435464,
                "value": 0x00010304 # rtHydraHarpT3
            }
        ]
    }
    header.json = json.dumps(header_dict)
    data.write('spc_data_converted.ptu')
    data_ptu = tttrlib.TTTR(ptu_file)


When a TTTR file is writen to another format certain meta data need to be provided.
The combination of tttr_container_type and tttr_record_type determines of the header
determines the ouput format of the TTTR writer method.

For PTU files at least the instrument and the measurement mode (T2, T3) need to be
provided.

.. code-block:: cpp

    #define rtPicoHarpT3     0x00010303    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $03 (T3), HW: $03 (PicoHarp)
    #define rtPicoHarpT2     0x00010203    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $03 (PicoHarp)
    #define rtHydraHarpT3    0x00010304    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $03 (T3), HW: $04 (HydraHarp)
    #define rtHydraHarpT2    0x00010204    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $04 (HydraHarp)
    #define rtHydraHarp2T3   0x01010304    // (SubID = $01 ,RecFmt: $01) (V2), T-Mode: $03 (T3), HW: $04 (HydraHarp)
    #define rtHydraHarp2T2   0x01010204    // (SubID = $01 ,RecFmt: $01) (V2), T-Mode: $02 (T2), HW: $04 (HydraHarp)
    #define rtTimeHarp260NT3 0x00010305    // (SubID = $00 ,RecFmt: $01) (V2), T-Mode: $03 (T3), HW: $05 (TimeHarp260N)
    #define rtTimeHarp260NT2 0x00010205    // (SubID = $00 ,RecFmt: $01) (V2), T-Mode: $02 (T2), HW: $05 (TimeHarp260N)
    #define rtTimeHarp260PT3 0x00010306    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T3), HW: $06 (TimeHarp260P)
    #define rtTimeHarp260PT2 0x00010206    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $06 (TimeHarp260P)
    #define rtMultiHarpNT3   0x00010307    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T3), HW: $07 (MultiHarp150N)
    #define rtMultiHarpNT2   0x00010207    // (SubID = $00 ,RecFmt: $01) (V1), T-Mode: $02 (T2), HW: $07 (MultiHarp150N)


The types of the meta data follows the PTU file convention.

.. code-block:: cpp

    #define tyEmpty8      0xFFFF0008
    #define tyBool8       0x00000008
    #define tyInt8        0x10000008
    #define tyBitSet64    0x11000008
    #define tyColor8      0x12000008
    #define tyFloat8      0x20000008
    #define tyTDateTime   0x21000008
    #define tyFloat8Array 0x2001FFFF
    #define tyAnsiString  0x4001FFFF
    #define tyWideString  0x4002FFFF
    #define tyBinaryBlob  0xFFFFFFFF


Writing manually correct and functional header files can be tedious. Hence tttrlib
offers the option to use header information and headers of other TTTR files.

.. code-block:: python

    import tttrlib
    data = tttrlib.TTTR('./data/bh/bh_spc132.spc', 'SPC-130')
    ptu_header = tttrlib.TTTRHeader('./data/pq/pq_ptu_hh_t3.ptu')
    output = {
        'filename': 'spc_data_converted.ptu',
        'header': ptu_header
    }
    data.write(**output)

.. note::
    The different TTTR container formats are not fully compatible. Hence, it can
    happen that certain information that is for instance stored in the header is
    lost when converting and saving data. For instance, BH 130 SPC files can hold
    up to 4096 micro time channels, while PQ-PTU files hold up to 32768 micro time
    channels.


