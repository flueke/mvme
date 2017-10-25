#!/usr/bin/env python3

import collections
import os.path
import struct
import sys

import matplotlib.pyplot as plt
import numpy as np

def funpack(f, fmt):
    data = f.read(struct.calcsize(fmt))
    return struct.unpack(fmt, data)

if __name__ == "__main__":
    filename = sys.argv[1]
    basename = os.path.splitext(filename)[0]

    H1DInfo = collections.namedtuple(
            'H1DInfo',
            ['size', 'binningMin', 'binningMax', 'underflow', 'overflow'],
            verbose=False)

    with open(filename, mode='rb') as f:
        histoCount = funpack(f, '=i')[0]
        print("histoCount: %d" % (histoCount))

        cols = 4
        rows = int(histoCount / cols)

        multi_fig, ax = plt.subplots(ncols=cols, nrows=rows)
        axes = ax.ravel() # flatten axis lists

        for histIndex in range(histoCount):
            info = H1DInfo(*funpack(f, '=idddd'))
            dataBytes = info.size * struct.calcsize('d')
            print("[%d] info: %s, %d bytes" %
                    (histIndex, info, dataBytes))

            data = f.read(dataBytes)

            if len(data) != dataBytes:
                raise "format error"

            np_x = np.arange(0, info.size)
            np_data = np.frombuffer(data, dtype=np.float64)
            #print("np_data[%d]: %s" % (histIndex, np_data))

            axes[histIndex].bar(
                    np_x,
                    np_data,
                    label=("[%d]" % (histIndex)))
            axes[histIndex].set_title("[%d]" % (histIndex))

            # single histo file
            fig = plt.figure()
            plt.bar(np_x, np_data)
            plt.title(basename + "[%d]" % (histIndex))
            plt.savefig(basename + str(histIndex) + ".png")

    # select the multi figure and save it
    plt.figure(multi_fig.number)
    plt.tight_layout()
    plt.savefig(basename + ".png")


    sys.exit(0)
