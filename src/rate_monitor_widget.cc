#include "rate_monitor_widget.h"

using RateMonitorNode = util::tree::Node<std::shared_ptr<RateMonitorNodeData>>;

struct RateMonitorWidgetPrivate
{
    QTreeWidget *m_rateTree;
    QGroupBox *m_propertyBox;

    QTableWidget *m_rateTable;
    RateMonitorPlotWidget *m_plotWidget;


    RateMonitorRegistry *m_registry;
    MVMEContext *m_context;

    QVector<RateSampler> m_entries;

    DAQStatsSampler m_daqStatsSampler;
    StreamProcessorSampler m_streamProcSampler;
    SIS3153Sampler m_sisReadoutSampler;

    // rate table stuff. TODO: move this to a RateTable abstraction at some point
    void addRateEntryToTable(RateSampler *entry, const QString &name);
    void removeRateEntryFromTable(RateSampler *entry);
    void updateRateTable();
    BiHash<RateSampler *, QTableWidgetItem *> m_rateTableHash; // FIXME: one item for each cell, not one item for a row!

    void dev_test_setup_thingy();
};
