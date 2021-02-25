import py_listfile_reader as lfr
import numpy as np

def begin_run(run):
    for event in run.getEvents():
        print("  ", event.name)
        for module in event.getModules():
            print("    ", module.name)

def get_module_arrays(m):
    return (np.asarray(m.prefix), np.asarray(m.dynamic), np.asarray(m.suffix))

def event_data(eventIndex, modules):
    print( "event_data for event%d:" % (eventIndex,))

    for moduleIndex in range(len(modules)):
        # Prefix and suffix data could be handled in the same way if needed.

        dynamicData = np.asarray(modules[moduleIndex].dynamic)

        if (len(dynamicData) > 0):
            print("  module_dyanmic for module%d" % (moduleIndex,))

            for value in dynamicData:
                print("    0x%08X" % (value, ))

def end_run(userobject):
    pass
