#!/usr/bin/env python3

MicroRegister = 0x102E
MicroHandShakeRegister = 0x1030

class HandShakeValues:
    WriteOk = 1
    ReadOk = 2

StatusBits = {
        0:  'data_ready',
        1:  'almost_full',
        2:  'full',
        3:  'trg_match',
        4:  'header_en',
        5:  'term_on',
        6:  'error0',
        7:  'error1',
        8:  'error2',
        9:  'error3',
        10: 'berr_flag',
        11: 'purg',
        12: 'resolution_0',
        13: 'resolution_1',
        14: 'pair_mode',
        15: 'trigger_lost',

        }

def decode_status(status):

    for bit, name in StatusBits.items():
        print("%15s (bit %2u): %u" % (name, bit, (status & (1 << bit)) != 0))

def make_opcode(cmd, obj = 0):
    return (cmd & 0xff) << 8 | (obj & 0xff)

def print_opcode_write(comment, cmd, obj = 0):
    print("# %s" % (comment,))
    print("0x102E 0x%04x" % (make_opcode(cmd, obj),))

if __name__ == "__main__":
    op = make_opcode(0xaf, 0xfe)
    print("0x%08x" % (op,))

    op = make_opcode(0x42)
    print("0x%08x" % (op,))

    print(StatusBits)
    print
    decode_status(0x00002439)

    print_opcode_write("window width (5.3.1)", 0x10)
    print_opcode_write("window offset (5.3.2)", 0x11)
    print_opcode_write("extra search margin (5.3.3)", 0x12)
    print_opcode_write("reject margin (5.3.4)", 0x13)
    print_opcode_write("enable subtraction of trigger time (5.3.5)", 0x14)
    print_opcode_write("disable subtraction of trigger time (5.3.6)", 0x15)
