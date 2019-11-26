def decode(value):
    for shift in (24, 16, 8, 0):
        b = (value >> shift) & 0xFF
        print b,
        if shift != 0:
            print ".",
        else:
            print

if __name__ == "__main__":
    while True:
        value = int(raw_input())
        decode(value)
