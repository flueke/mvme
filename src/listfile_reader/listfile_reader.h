#ifndef __MVME_LISTFILE_READER_LISTFILE_READER_H__
#define __MVME_LISTFILE_READER_LISTFILE_READER_H__

#include <stdint.h>

extern "C"
{

struct ModuleReadoutDescription
{
    const char *name;
    const char *type;
    unsigned prefixLen;
    unsigned suffixLen;
    bool hasDynamic;
};

struct EventReadoutDescription
{
    const char *name;
    ModuleReadoutDescription *modules;
    int moduleCount;
};

struct RunDescription
{
    const char *listfileFilename;
    EventReadoutDescription *events;
    int eventCount;
};

struct DataBlock
{
    const uint32_t *data;
    uint32_t size;
};

struct ModuleData
{
    DataBlock prefix;
    DataBlock dynamic;
    DataBlock suffix;
};

typedef void (*PluginInfo) (char **pluginName, char **pluginDescription);
typedef void * (*PluginInit) (const char *pluginFilename, int argc, const char *argv[]);
typedef void (*PluginDestroy) (void *userptr);

typedef void (*BeginRun) (void *userptr, const RunDescription *run);
typedef void (*EventData) (void *userptr, int eventIndex, const ModuleData *modules, int moduleCount);
typedef void (*EndRun) (void *userptr);

}

#endif /* __MVME_LISTFILE_READER_LISTFILE_READER_H__ */
