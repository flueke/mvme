#ifndef __RUN_INFO_H__
#define __RUN_INFO_H__

#include <QString>

/* Information about the current DAQ run or the run that's being replayed from
 * a listfile. */
struct RunInfo
{
    /* This is the full runId string. It is used to generate the listfile
     * archive name and the listfile filename inside the archive. */
    QString runId;

    /* Set to true to retain histogram contents across replays. Keeping the
     * contents only works if the number of bins and the binning do not change
     * between runs. If set to false all histograms will be cleared before the
     * replay starts. */
    // TODO: replace with flags
    bool keepAnalysisState = false;
    bool isReplay = false;
    //bool generateExportFiles = false;

};

inline bool operator==(const RunInfo &a, const RunInfo &b)
{
    return a.runId == b.runId
        && a.keepAnalysisState == b.keepAnalysisState
        && a.isReplay == b.isReplay;
}

inline bool operator!=(const RunInfo &a, const RunInfo &b)
{
    return !(a == b);
}

#endif /* __RUN_INFO_H__ */
