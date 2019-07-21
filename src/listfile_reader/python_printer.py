print("Hello from the python_printer!")

import py_listfile_reader as lfr
import numpy as np
#import time

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

    #np_prefix = np.asarray(run.prefix)
    #print(np_prefix)
    
    #print(np_array)
    #print(len(run))
    #raise Exception("foobar")
    return 42;

def get_module_arrays(m):
    return (np.asarray(m.prefix), np.asarray(m.dynamic), np.asarray(m.suffix))

def event_data(eventIndex, modules):
    print(eventIndex)

    for mi, m in zip(range(len(modules)), modules):
        print("  ", mi, m, get_module_arrays(m))

    #print(eventIndex, modules)
    pass

def end_run(userobject):
    print(userobject)
