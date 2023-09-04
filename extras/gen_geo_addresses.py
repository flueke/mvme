#!/usr/bin/env python3

import argparse
import sys

if __name__ == "__main__":
    parser = argparse.ArgumentParser(
            formatter_class=argparse.ArgumentDefaultsHelpFormatter)

    parser.add_argument('--first_slot', metavar='SLOT', type=int, default=2)
    parser.add_argument('--last_slot', metavar='SLOT', type=int, default=20)
    args = parser.parse_args()

    for slot in range(args.first_slot, args.last_slot+2):
        print("0x{:08x}".format(slot * 0x01000000))
