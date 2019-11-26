print("Hello from the python_printer!")

import py_listfile_reader as lfr
import numpy as np
#import time

prevCounterValue = 0

def begin_run(run):
    print(dir(lfr.ModuleReadoutDescription))
    print(dir(lfr.DataBlock))


    print("---- Run:\n", run)
    print(dir(run))
    print(run.getEvents())

    for event in run.getEvents():
        print("  ", event.name)
        for module in event.getModules():
            print("    ", module.name)

    global prevCounterValue
    prevCounterValue = 0

    #np_prefix = np.asarray(run.prefix)
    #print(np_prefix)

    #print(np_array)
    #print(len(run))
    #raise Exception("foobar")
    return 42;

def get_module_arrays(m):
    return (np.asarray(m.prefix), np.asarray(m.dynamic), np.asarray(m.suffix))

def event_data(eventIndex, modules):
    #print(eventIndex)

    #for mi, m in zip(range(len(modules)), modules):
    #    print("  ", mi, m, get_module_arrays(m))

    counterLo, counterHi = get_module_arrays(modules[0])[0]
    counterLo = counterLo & 0xffff
    counterHi = counterHi & 0xffff
    counterValue = (counterHi << 16) | counterLo;

    global prevCounterValue
    counterDelta = counterValue - prevCounterValue;

    #print("[lo]: %u, %s" % (counterLo, bin(counterLo)))
    #print("[hi]: %u, %s" % (counterHi, bin(counterHi)))

    print("counterValue: %u, %s" % (counterValue, bin(counterValue)))
    print("prevCounterValue: %u, %s" % (prevCounterValue, bin(prevCounterValue)))
    print("delta=%u" % (counterDelta,))

    prevCounterValue = counterValue

    #print(eventIndex, modules)
    pass

def end_run(userobject):
    print(userobject)
