print("Hello from the python_printer!")

import py_listfile_reader as lfr
import numpy as np
#import time

def begin_run(run):
    print(dir(lfr))
    print(dir(lfr.ModuleReadoutDescription))
    print(dir(lfr.DataBlock))
    print(dir(np))


    print(run)

    np_array = np.asarray(run)
    
    print(np_array)
    #print(len(run))
    #raise Exception("foobar")

def event_data(eventIndex, modules):
    #print(eventIndex, modules)
    pass

def end_run():
    print(run)
