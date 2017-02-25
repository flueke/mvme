#include "hist2ddialog.h"
#include "ui_hist2ddialog.h"
#include "ui_hist2ddialog_axis_widget.h"

#include <cmath>

#include <QPushButton>
#include <QSignalBlocker>
#include <QTreeWidget>

class TreeNode: public QTreeWidgetItem
{
    public:
        using QTreeWidgetItem::QTreeWidgetItem;
};

enum NodeType
{
    NodeType_FilterAddress = QTreeWidgetItem::UserType,
};

enum DataRole
{
    DataRole_Pointer = Qt::UserRole,
    DataRole_FilterAddress,
};

//
// SelectAxisSourceDialog
//
SelectAxisSourceDialog::SelectAxisSourceDialog(MVMEContext *context, int selectedEventIndex, QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(QSL("Select axis source"));

    auto treeWidget = new QTreeWidget;
    m_tree = treeWidget;
    treeWidget->headerItem()->setHidden(true);
    treeWidget->setIndentation(10);

    auto daqConfig = context->getDAQConfig();
#ifdef ENABLE_OLD_ANALYSIS
    auto analysisConfig = context->getAnalysisConfig();
    auto filters = analysisConfig->getFilters();

    for (int eventIndex: filters.keys())
    {
        if (selectedEventIndex >= 0 && eventIndex != selectedEventIndex)
            continue;

        auto eventConfig = daqConfig->getEventConfig(eventIndex);
        auto eventNode = new TreeNode;
        eventNode->setText(0, eventConfig ? eventConfig->objectName() : QString::number(eventIndex));
        eventNode->setIcon(0, QIcon(":/config_category.png"));
        treeWidget->addTopLevelItem(eventNode);

        for (int moduleIndex: filters[eventIndex].keys())
        {
            auto moduleConfig = daqConfig->getModuleConfig(eventIndex, moduleIndex);
            auto moduleNode = new TreeNode;
            moduleNode->setText(0, moduleConfig ? moduleConfig->objectName() : QString::number(moduleIndex));
            moduleNode->setIcon(0, QIcon(":/vme_module.png"));
            eventNode->addChild(moduleNode);

            for (auto filterConfig: filters[eventIndex][moduleIndex])
            {
                auto filterNode = new TreeNode;
                filterNode->setText(0, filterConfig->objectName());
                filterNode->setIcon(0, QIcon(":/data_filter.png"));
                moduleNode->addChild(filterNode);
                const auto &filter = filterConfig->getFilter();
                u32 addressCount = 1 << filter.getExtractBits('A');

                for (u32 address = 0; address < addressCount; ++address)
                {
                    auto addressNode = new TreeNode(NodeType_FilterAddress);
                    addressNode->setText(0, QString::number(address));
                    addressNode->setData(0, DataRole_Pointer, Ptr2Var(filterConfig));
                    addressNode->setData(0, DataRole_FilterAddress, address);
                    addressNode->setIcon(0, QIcon(":/hist1d.png"));
                    filterNode->addChild(addressNode);
                }
            }

            moduleNode->setExpanded(true);
        }

        eventNode->setExpanded(true);
    }
#endif

    connect(treeWidget, &QTreeWidget::currentItemChanged, this, &SelectAxisSourceDialog::onTreeCurrentItemChanged);
    connect(treeWidget, &QTreeWidget::itemDoubleClicked, this, &SelectAxisSourceDialog::onItemDoubleClicked);

    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttonBox = buttonBox;
    buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    connect(buttonBox, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto layout = new QVBoxLayout(this);
    layout->addWidget(treeWidget);
    layout->addWidget(buttonBox);
}

void SelectAxisSourceDialog::onTreeCurrentItemChanged(QTreeWidgetItem *current, QTreeWidgetItem *previous)
{
    bool isFilterAddressNode = (current->type() == NodeType_FilterAddress);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(isFilterAddressNode);
}

void SelectAxisSourceDialog::onItemDoubleClicked(QTreeWidgetItem *item, int column)
{
    bool isFilterAddressNode = (item->type() == NodeType_FilterAddress);

    if (isFilterAddressNode)
        accept();
}

void SelectAxisSourceDialog::accept()
{
    QDialog::accept();
}

QPair<DataFilterConfig *, int> SelectAxisSourceDialog::getAxisSource() const
{
    auto node = m_tree->currentItem();
    if (node->type() == NodeType_FilterAddress)
    {
        return qMakePair(Var2Ptr<DataFilterConfig>(node->data(0, DataRole_Pointer)),
                         node->data(0, DataRole_FilterAddress).toInt());
    }
    return QPair<DataFilterConfig *, int>(nullptr, -1);
}

//
// util
//
static QwtInterval clean_interval(const QwtInterval &interval, int bits)
{
    auto result = QwtInterval(
        std::floor(interval.minValue()),
        std::ceil(interval.maxValue()));

    if (result.minValue() < 0.0)
        result.setMinValue(0.0);

    const double maxBin = std::pow(2.0, bits);

    if (result.maxValue() > maxBin)
        result.setMaxValue(maxBin);

    return result;
};

static void fill_resolution_combo(QComboBox *combo, int minBits, int maxBits, int selectedBits = 0)
{
    combo->clear();

    for (int bits=minBits; bits<=maxBits; ++bits)
    {
        int value = 1 << bits;
        QString text = QString("%1, %2 bit")
            .arg(value, 4)
            .arg(bits, 2);

        combo->addItem(text, bits);
    }

    if (selectedBits > 0)
    {
        int index = selectedBits - minBits;
        index = std::min(index, combo->count() - 1);
        combo->setCurrentIndex(index);
    }
}

//
// Hist2DDialog
//
/* Modes:
 * Create
 *   - unit spins are filled with full range
 *   - resolution is set to max of the filter source (limited to 13 bits)
 *   - changing the unit spins results in an offset being added to the resulting histo
 *
 *  Edit
 *   - unit spins are filled with the values set on the histo
 *   - resolution is set to the values set on the histo
 *   - changing the unit spins will result in a new offset and possibly a different resolution
 *
 *  Sub
 *   - used to create a sub-histogram from the given histo and the given bin-intervals
 *   - the axes of the newly created histo start at the minimum of the corresponding interval
 *   - the upper limit of the interval is used to calculate the number of bins needed to store the requested range.
 *     This number is then rounded up to next power of two. The resulting histo will have an offset and possibly a
 *     shift different from the source histo.
 *   - The unit spins are filled with unit values caclulated by taking the given bin-intervals, upscaling them to
 *     full resolution and convertion those full res bins to unit values.
 *
 * Note: the unitMax spin is used to calculate the required resolution to
 * minimally store that range. Lower resolutions can be selected and result in
 * a shift being added to the histo. The unit max value may be exceeded as the
 * resolution will always be the next biggest power of two.
 */

Hist2DDialog::Hist2DDialog(MVMEContext *context, QWidget *parent)
    : Hist2DDialog(Create, context, nullptr, QwtInterval(), QwtInterval(), parent)
{
}

Hist2DDialog::Hist2DDialog(MVMEContext *context, Hist2D *histo, QWidget *parent)
    : Hist2DDialog(Edit, context, histo, QwtInterval(), QwtInterval(), parent)
{
    Q_ASSERT(histo);
    Q_ASSERT(m_histoConfig);
}

Hist2DDialog::Hist2DDialog(MVMEContext *context, Hist2D *histo,
                 QwtInterval xBinRange, QwtInterval yBinRange,
                 QWidget *parent)
    : Hist2DDialog(Sub, context, histo, xBinRange, yBinRange, parent)
{
    Q_ASSERT(histo);
    Q_ASSERT(m_histoConfig);
}

Hist2DDialog::Hist2DDialog(Mode mode, MVMEContext *context, Hist2D *histo,
             QwtInterval xBinRange, QwtInterval yBinRange,
             QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Hist2DDialog)
    , m_mode(mode)
    , m_context(context)
    , m_histo(histo)
    , m_histoConfig(nullptr)
    , m_xSource(nullptr, -1)
    , m_ySource(nullptr, -1)
    , m_xBinRange(xBinRange)
    , m_yBinRange(yBinRange)
    , m_result(nullptr, nullptr)
{
    Q_ASSERT(context);

    ui->setupUi(this);

    auto makeAxisUi = [this](Qt::Axis axis, QWidget *dest)
    {
        auto axisUi = new Ui::AxisDataWidget;
        auto widget = new QWidget;
        axisUi->setupUi(widget);
        auto layout = new QHBoxLayout(dest);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->addWidget(widget);

        connect(axisUi->pb_selectSource, &QPushButton::clicked,
                this, [this, axis]() { onSelectSourceClicked(axis); });

        connect(axisUi->pb_clearSource, &QPushButton::clicked,
                this, [this, axis]() { onClearSourceClicked(axis); });

        connect(axisUi->spin_unitMin, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                this, [this, axis] (double) { onUnitRangeChanged(axis); });

        connect(axisUi->spin_unitMax, static_cast<void(QDoubleSpinBox::*)(double)>(&QDoubleSpinBox::valueChanged),
                this, [this, axis] (double) { onUnitRangeChanged(axis); });

        connect(axisUi->combo_resolution, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                this, [this, axis] (int) { onResolutionSelected(axis); });

        if (m_mode == Sub)
        {
            axisUi->pb_selectSource->setEnabled(false);
            axisUi->pb_clearSource->setEnabled(false);
        }

        return axisUi;
    };

    m_xAxisUi = makeAxisUi(Qt::XAxis, ui->gb_xAxis);
    m_yAxisUi = makeAxisUi(Qt::YAxis, ui->gb_yAxis);

    if (m_histo)
    {
        m_histoConfig = qobject_cast<Hist2DConfig *>(m_context->getConfigForObject(m_histo));

        if (m_histoConfig)
        {
            auto histoName = m_histoConfig->objectName();

            if (m_mode == Sub)
            {
                QRegularExpression re("^(.*\\ )(\\d+)$");
                auto match = re.match(histoName);
                if (match.lastCapturedIndex() == 2)
                {
                    histoName = match.captured(1) + QString::number(match.captured(2).toInt() + 1);
                }
                else
                {
                    histoName += QSL(" 1");
                }
            }

            ui->le_name->setText(histoName);

#ifdef ENABLE_OLD_ANALYSIS
            {
                auto filterConfig = m_context->getAnalysisConfig()->findChildById<DataFilterConfig *>(m_histoConfig->getFilterId(Qt::XAxis));
                auto address = m_histoConfig->getFilterAddress(Qt::XAxis);
                m_xSource = qMakePair(filterConfig, address);
            }
            {
                auto filterConfig = m_context->getAnalysisConfig()->findChildById<DataFilterConfig *>(m_histoConfig->getFilterId(Qt::YAxis));
                auto address = m_histoConfig->getFilterAddress(Qt::YAxis);
                m_ySource = qMakePair(filterConfig, address);
            }
#endif

            if (m_xBinRange.isValid())
                m_xBinRange = clean_interval(m_xBinRange, m_histoConfig->getBits(Qt::XAxis));

            if (m_yBinRange.isValid())
                m_yBinRange = clean_interval(m_yBinRange, m_histoConfig->getBits(Qt::YAxis));

        }
    }

    auto validator = new NameValidator(context, (m_mode == Edit ? histo : nullptr), this);
    ui->le_name->setValidator(validator);

    connect(ui->le_name, &QLineEdit::textChanged, this, [this](const QString &) {
        validate();
    });

    onSourceSelected(Qt::XAxis);
    onSourceSelected(Qt::YAxis);
    validate();
}

Hist2DDialog::~Hist2DDialog()
{
    delete ui;
    delete m_xAxisUi;
    delete m_yAxisUi;
}

QPair<Hist2D *, Hist2DConfig *> Hist2DDialog::getHistoAndConfig()
{
    int xBits = m_xAxisUi->combo_resolution->currentData().toInt();
    int yBits = m_yAxisUi->combo_resolution->currentData().toInt();
    Hist2D *histo = nullptr;
    Hist2DConfig *histoConfig = nullptr;

    if (m_mode == Create || m_mode == Sub)
    {
        histo = new Hist2D(xBits, yBits);
        histoConfig = new Hist2DConfig;
    }
    else if (m_mode == Edit)
    {
        histoConfig = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(m_histo, QSL("ObjectToConfig")));
        histo = m_histo;
        histo->resize(xBits, yBits);
    }

    if (histoConfig)
    {
        histo->setObjectName(ui->le_name->text());
        histoConfig->setObjectName(ui->le_name->text());

        auto setConfigValues = [this] (Hist2DConfig *histoConfig, Qt::Axis axis, int selectedBits)
        {
            auto source = (axis == Qt::XAxis ? m_xSource : m_ySource);
            auto axisUi = (axis == Qt::XAxis ? m_xAxisUi : m_yAxisUi);
            auto filter = source.first;
            auto address = source.second;
            auto title = filter->getAxisTitle();

            if (title.isEmpty())
                title = QString("%1/%2") .arg(filter->objectName()) .arg(address);

            int sourceBits = filter->getDataBits();

            double unitMin = axisUi->spin_unitMin->value();
            double unitMax = axisUi->spin_unitMax->value();

            auto conversion = filter->makeConversionMap(address);

            double lowerBin = conversion.invTransform(unitMin);
            double upperBin = conversion.invTransform(unitMax);
            double binRange = upperBin - lowerBin;

            // the number of bits needed to store the selected range in full resolution
            int bits  = std::ceil(std::log2(binRange));
            
            // the resulting number of stored bins in full res
            binRange = 1 << bits;

            // fix unit max to account for the number of bins that are actually stored
            unitMax = conversion.transform(lowerBin + binRange - 1.0);

            // calculate shift if something lower than full res was selected
            int shift = 0;

            if (bits > selectedBits)
            {
                shift = bits - selectedBits;
                bits = selectedBits;
            }

            histoConfig->setFilterId(axis, filter->getId());
            histoConfig->setFilterAddress(axis, address);
            histoConfig->setBits(axis, bits);
            histoConfig->setShift(axis, shift);
            histoConfig->setOffset(axis, lowerBin);
            histoConfig->setAxisTitle(axis, title);
            histoConfig->setAxisUnitLabel(axis, filter->getUnitString());
            histoConfig->setUnitMin(axis, unitMin);
            histoConfig->setUnitMax(axis, unitMax);
        };

        setConfigValues(histoConfig, Qt::XAxis, xBits);
        setConfigValues(histoConfig, Qt::YAxis, yBits);
    }

    return qMakePair(histo, histoConfig);
}

bool Hist2DDialog::validate()
{
    bool valid = (m_xSource.first && m_ySource.first && ui->le_name->hasAcceptableInput());
    valid = valid && m_xAxisUi->combo_resolution->count() && m_yAxisUi->combo_resolution->count();

    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
    return valid;
}

void Hist2DDialog::updateSourceLabels()
{
    m_xAxisUi->label_source->setText(QSL("<none selected>"));
    m_yAxisUi->label_source->setText(QSL("<none selected>"));

    if (m_xSource.first)
    {
        m_xAxisUi->label_source->setText(getFilterPath(m_context, m_xSource.first, m_xSource.second));
    }

    if (m_ySource.first)
    {
        m_yAxisUi->label_source->setText(getFilterPath(m_context, m_ySource.first, m_ySource.second));
    }
}

void Hist2DDialog::onSelectSourceClicked(Qt::Axis axis)
{
#ifdef ENABLE_OLD_ANALYSIS
    const auto &otherAxisSource = (axis == Qt::XAxis ? m_ySource : m_xSource);

    int eventIndex = -1;
    if (otherAxisSource.first)
    {
        eventIndex = m_context->getAnalysisConfig()->getEventAndModuleIndices(otherAxisSource.first).first;
    }

    qDebug() << __PRETTY_FUNCTION__ <<  "eventIndex" << eventIndex;

    SelectAxisSourceDialog dialog(m_context, eventIndex, this);

    if (dialog.exec() == QDialog::Accepted)
    {
        auto &source = (axis == Qt::XAxis ? m_xSource : m_ySource);
        auto prevSource = source;
        source = dialog.getAxisSource();
        onSourceSelected(axis, prevSource);
    }
#endif
}

void Hist2DDialog::onClearSourceClicked(Qt::Axis axis)
{
    auto &source = (axis == Qt::XAxis ? m_xSource : m_ySource);
    source = AxisSource(nullptr, -1);
    onSourceSelected(axis);
}

void Hist2DDialog::onSourceSelected(Qt::Axis axis, AxisSource prevSource)
{
    auto axisUi = (axis == Qt::XAxis ? m_xAxisUi : m_yAxisUi);
    const auto &source = (axis == Qt::XAxis ? m_xSource : m_ySource);

    double unitLimitMin = 0.0;
    double unitLimitMax = 0.0;
    double unitMin = 0.0;
    double unitMax = 0.0;
    QString label;

    if (source.first)
    {
        unitLimitMin = source.first->getUnitMin(source.second);
        unitLimitMax = source.first->getUnitMax(source.second);
        label   = source.first->getUnitString();

        if (!label.isEmpty())
            label = "[" + label + "]";
    }

    {
        QSignalBlocker b1(axisUi->spin_unitMin);
        QSignalBlocker b2(axisUi->spin_unitMax);

        axisUi->spin_unitMin->setMinimum(unitLimitMin);
        axisUi->spin_unitMin->setMaximum(unitLimitMax);
        axisUi->spin_unitMin->setValue(unitLimitMin);

        axisUi->spin_unitMax->setMinimum(unitLimitMin);
        axisUi->spin_unitMax->setMaximum(unitLimitMax);
        axisUi->spin_unitMax->setValue(unitLimitMax);

        if (m_mode == Edit && source.first && source.first->getId() == m_histoConfig->getFilterId(axis))
        {
            axisUi->spin_unitMin->setValue(m_histoConfig->getUnitMin(axis));
            axisUi->spin_unitMax->setValue(m_histoConfig->getUnitMax(axis));
        }
        else if (m_mode == Sub)
        {
            auto binRange = (axis == Qt::XAxis ? m_xBinRange : m_yBinRange);
            qDebug() << __PRETTY_FUNCTION__ << "binRange" << binRange;
            double lowerBin = binRange.minValue();
            double upperBin = binRange.maxValue();
            auto shift  = m_histoConfig->getShift(axis);
            auto offset = m_histoConfig->getOffset(axis);

            // convert to full resolution bin numbers
            lowerBin = lowerBin * std::pow(2.0, shift) + offset;
            upperBin = upperBin * std::pow(2.0, shift) + offset;

            // and use the full res bins to convert back into units
            auto conversion = source.first->makeConversionMap(source.second);
            axisUi->spin_unitMin->setValue(conversion.transform(lowerBin));
            axisUi->spin_unitMax->setValue(conversion.transform(upperBin));

        }
    }

    axisUi->label_unit->setText(label);

    updateSourceLabels();
    updateResolutionCombo(axis);
    validate();
}

void Hist2DDialog::onUnitRangeChanged(Qt::Axis axis)
{
    qDebug() << __PRETTY_FUNCTION__;
    updateResolutionCombo(axis);
}

void Hist2DDialog::updateResolutionCombo(Qt::Axis axis)
{
    static const int defaultMinBits =  1;
    static const int defaultMaxBits = 13; // hard limit for the number of bits used per axis
    static const int defaultBits    = 10;

    auto axisUi = (axis == Qt::XAxis ? m_xAxisUi : m_yAxisUi);

    int maxBits      = defaultMaxBits;
    int selectedBits = defaultBits;

    if (axisUi->combo_resolution->count())
    {
        // keep selected bits
        auto var = axisUi->combo_resolution->currentData();
        if (var.isValid())
            selectedBits = var.toInt();
    }
    else if (m_mode == Edit && m_histoConfig)
    {
        selectedBits = m_histoConfig->getBits(axis);
    }

    const auto &source = (axis == Qt::XAxis ? m_xSource : m_ySource);

    if (source.first)
    {
        auto filterConfig = source.first;
        maxBits = filterConfig->getDataBits();
        auto conversion = filterConfig->makeConversionMap(source.second);

        
        double unitMin = axisUi->spin_unitMin->value();
        double unitMax = axisUi->spin_unitMax->value();


        double lowerBin = std::floor(conversion.invTransform(axisUi->spin_unitMin->value()));
        double upperBin = std::ceil(conversion.invTransform(axisUi->spin_unitMax->value()));
        double binRange = upperBin - lowerBin;

#if 0
        qDebug() << __PRETTY_FUNCTION__
            << "scaleInterval" << conversion.s1() << conversion.s2()
            << "paintInterval" << conversion.p1() << conversion.p2();

        qDebug() << __PRETTY_FUNCTION__
            << "unitMin" << unitMin
            << "unitMax" << unitMax;

        qDebug() << __PRETTY_FUNCTION__
            << "lowerBin" << lowerBin
            << "upperBin" << upperBin
            << "binRange" << binRange;
#endif

        // the number of bits needed to store the selected range in full resolution
        maxBits = std::ceil(std::log2(binRange + 1.0));
        maxBits = std::min(maxBits, defaultMaxBits);
    }

    fill_resolution_combo(axisUi->combo_resolution, defaultMinBits, maxBits, selectedBits);
}

void Hist2DDialog::onResolutionSelected(Qt::Axis axis)
{
    ui->label_memory->clear();

    bool xOk, yOk;

    int xBits = m_xAxisUi->combo_resolution->currentData().toInt(&xOk);
    int yBits = m_yAxisUi->combo_resolution->currentData().toInt(&yOk);

    if (xOk && yOk)
    {
        size_t xBins = 1 << xBits;
        size_t yBins = 1 << yBits;
        size_t mem = xBins * yBins * sizeof(double);

        auto sizeAndUnit = byte_unit(mem);

        ui->label_memory->setText(QString("%L1 %2")
                                  .arg(sizeAndUnit.first, 0, 'f', 2)
                                  .arg(sizeAndUnit.second));
    }
}
