#!/usr/bin/env python
import argparse
import os
import numpy as np
import tttrlib


if __name__ == "__main__":

    parser = argparse.ArgumentParser(description='Compute a decay histogram.')

    parser.add_argument(
        'channel',
        metavar='channel',
        type=int,
        default=1,
        help='The detection channel number'
    )

    parser.add_argument(
        'coarse',
        metavar='coarse',
        type=int,
        default=2,
        help='A divider for the micro time (default 2)'
    )

    parser.add_argument(
        'file_type',
        metavar='type',
        type=str,
        help='The TTTR file type either HT3, PTU, '
             'SPC-130, SPC-600_256, SPC-600_4096, or PHOTON-HDF5'
    )

    parser.add_argument(
        'filename',
        metavar='file',
        type=str,
        help='The TTTR filename used to compute the decay histogram'
    )

    parser.add_argument(
        '-o',
        '--output',
        action='store_true',
        help='The output filename'
    )

    args = parser.parse_args()

    if not args.output:
        root, ext = os.path.splitext(
            os.path.abspath(args.filename)
        )
        output = root + '.csv'
    else:
        output = args.output

    print("Make decay histogram from TTTR data")
    print("===================================")
    print("\tFilename: %s" % args.filename)
    print("\tFile type: %s" % args.file_type)
    print("\tCoarse :\t%s" % args.coarse)
    print("\tOuput file: %s" % output)

    data = tttrlib.TTTR(
        args.filename,
        args.file_type
    )

    channel_selection = data.get_selection_by_channel(np.array([args.channel]))
    micro_time = data.get_micro_time()
    mt_sel = micro_time[channel_selection]
    counts = np.bincount(mt_sel // args.coarse)
    header = data.get_header()
    dt = header.micro_time_resolution
    x_axis = np.arange(counts.shape[0]) * dt * args.coarse
    output_filename = 'test_irf.csv'
    np.savetxt(
        fname=output,
        X=np.vstack([x_axis, counts]).T
    )



