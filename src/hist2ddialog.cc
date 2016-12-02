#include "hist2ddialog.h"
#include "ui_hist2ddialog.h"
#include "ui_hist2ddialog_axis_widget.h"

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
        combo->setCurrentIndex(selectedBits - minBits);
}

//
// Hist2DDialog
//

Hist2DDialog::Hist2DDialog(MVMEContext *context, QWidget *parent)
    : Hist2DDialog(Create, context, nullptr, QwtInterval(), QwtInterval(), parent)
{
}

Hist2DDialog::Hist2DDialog(MVMEContext *context, Hist2D *histo, QWidget *parent)
    : Hist2DDialog(Edit, context, histo, QwtInterval(), QwtInterval(), parent)
{
}

Hist2DDialog::Hist2DDialog(MVMEContext *context, Hist2D *histo,
                 QwtInterval xBinRange, QwtInterval yBinRange,
                 QWidget *parent)
    : Hist2DDialog(Sub, context, histo, xBinRange, yBinRange, parent)
{
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
    ui->setupUi(this);

    {
        m_xAxisUi = new Ui::AxisDataWidget;
        auto widget = new QWidget;
        m_xAxisUi->setupUi(widget);
        auto layout = new QHBoxLayout(ui->gb_xAxis);
        layout->addWidget(widget);

        connect(m_xAxisUi->pb_selectSource, &QPushButton::clicked,
                this, [this]() { onSelectSourceClicked(Qt::XAxis); });
        connect(m_xAxisUi->pb_clearSource, &QPushButton::clicked,
                this, [this]() { onClearSourceClicked(Qt::YAxis); });
    }

    {
        m_yAxisUi = new Ui::AxisDataWidget;
        auto widget = new QWidget;
        m_yAxisUi->setupUi(widget);
        auto layout = new QHBoxLayout(ui->gb_yAxis);
        layout->addWidget(widget);

        connect(m_yAxisUi->pb_selectSource, &QPushButton::clicked,
                this, [this]() { onSelectSourceClicked(Qt::YAxis); });
        connect(m_yAxisUi->pb_clearSource, &QPushButton::clicked,
                this, [this]() { onClearSourceClicked(Qt::YAxis); });
    }

    if (m_histo)
    {
        m_histoConfig = qobject_cast<Hist2DConfig *>(m_context->getConfigForObject(m_histo));

        if (m_histoConfig)
        {
            ui->le_name->setText(m_histoConfig->objectName());
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

            if (m_xBinRange.isValid())
                m_xBinRange = clean_interval(m_xBinRange, m_histoConfig->getBits(Qt::XAxis));

            if (m_yBinRange.isValid())
                m_yBinRange = clean_interval(m_yBinRange, m_histoConfig->getBits(Qt::YAxis));
        }
    }

    auto validator = new NameValidator(context, histo, this);
    ui->le_name->setValidator(validator);

    connect(ui->le_name, &QLineEdit::textChanged, this, [this](const QString &) {
        updateAndValidate();
    });

    updateResolutionCombo(Qt::XAxis);
    updateResolutionCombo(Qt::YAxis);
    updateAndValidate();
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
    Hist2DConfig *histoConfig = nullptr;

    if (!m_histo)
    {
        m_histo = new Hist2D(xBits, yBits);
        histoConfig = new Hist2DConfig;
    }
    else
    {
        histoConfig = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(m_histo, QSL("ObjectToConfig")));
        m_histo->resize(xBits, yBits);
    }

    if (histoConfig)
    {
        m_histo->setObjectName(ui->le_name->text());
        histoConfig->setObjectName(ui->le_name->text());

        // x axis
        {
            auto filter = m_xSource.first;
            auto address = m_xSource.second;
            auto title = filter->getAxisTitle();
            if (title.isEmpty())
                title = QString("%1/%2") .arg(filter->objectName()) .arg(address);

            int dataBits = filter->getFilter().getExtractBits('D');

            histoConfig->setFilterId(Qt::XAxis, filter->getId());
            histoConfig->setFilterAddress(Qt::XAxis, address);
            histoConfig->setBits(Qt::XAxis, xBits);
            histoConfig->setShift(Qt::XAxis, std::max(dataBits - xBits, 0));
            histoConfig->setAxisTitle(Qt::XAxis, title);
            histoConfig->setAxisUnitLabel(Qt::XAxis, filter->getUnitString());
            histoConfig->setUnitMin(Qt::XAxis, filter->getUnitMinValue());
            histoConfig->setUnitMax(Qt::XAxis, filter->getUnitMaxValue());

            qDebug() << __PRETTY_FUNCTION__ << "xShift" << histoConfig->getShift(Qt::XAxis);
        }

        // y axis
        {
            auto filter = m_ySource.first;
            auto address = m_ySource.second;
            auto title = filter->getAxisTitle();
            if (title.isEmpty())
                title = QString("%1/%2").arg(filter->objectName()).arg(address);

            int dataBits = filter->getFilter().getExtractBits('D');

            histoConfig->setFilterId(Qt::YAxis, filter->getId());
            histoConfig->setFilterAddress(Qt::YAxis, address);
            histoConfig->setBits(Qt::YAxis, yBits);
            histoConfig->setShift(Qt::YAxis, std::max(dataBits - yBits, 0));
            histoConfig->setAxisTitle(Qt::YAxis, title);
            histoConfig->setAxisUnitLabel(Qt::YAxis, filter->getUnitString());
            histoConfig->setUnitMin(Qt::YAxis, filter->getUnitMinValue());
            histoConfig->setUnitMax(Qt::YAxis, filter->getUnitMaxValue());

            qDebug() << __PRETTY_FUNCTION__ << "yShift" << histoConfig->getShift(Qt::YAxis);
        }
    }

    return qMakePair(m_histo, histoConfig);
}

void Hist2DDialog::onSelectSourceClicked(Qt::Axis axis)
{
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
        source = dialog.getAxisSource();
        onSourceSelected(axis);
        updateAndValidate();
    }
}

void Hist2DDialog::onClearSourceClicked(Qt::Axis axis)
{
    auto &source = (axis == Qt::XAxis ? m_xSource : m_ySource);
    source = QPair<DataFilterConfig *, int>(nullptr, -1);
    onSourceSelected(axis);
    updateAndValidate();
}

void Hist2DDialog::updateAndValidate()
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

    bool valid = (m_xSource.first && m_ySource.first && ui->le_name->hasAcceptableInput());
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
}

void Hist2DDialog::onSourceSelected(Qt::Axis axis)
{
    auto axisUi = (axis == Qt::XAxis ? m_xAxisUi : m_yAxisUi);
    const auto &source = (axis == Qt::XAxis ? m_xSource : m_ySource);

    double unitMin = 0.0;
    double unitMax = 0.0;
    QString label;

    if (source.first)
    {
        unitMin = source.first->getUnitMinValue();
        unitMax = source.first->getUnitMaxValue();
        label   = source.first->getUnitString();
        if (!label.isEmpty())
            label = "[" + label + "]";
    }

    axisUi->spin_unitMin->setMinimum(unitMin);
    axisUi->spin_unitMin->setMaximum(unitMax);
    axisUi->spin_unitMax->setMinimum(unitMin);
    axisUi->spin_unitMax->setMaximum(unitMax);

    axisUi->spin_unitMin->setValue(unitMin);
    axisUi->spin_unitMax->setValue(unitMax);
    axisUi->label_unit->setText(label);
}

void Hist2DDialog::updateResolutionCombo(Qt::Axis axis)
{
    static const int defaultMinBits =  1;
    static const int defaultMaxBits = 13;
    static const int defaultBits    = 10;

    auto axisUi = (axis == Qt::XAxis ? m_xAxisUi : m_yAxisUi);

    int maxBits      = defaultMaxBits;
    int selectedBits = defaultBits;

    if (m_mode == Edit && m_histoConfig)
        selectedBits = m_histoConfig->getBits(axis);

    const auto &source = (axis == Qt::XAxis ? m_xSource : m_ySource);

    // TODO: check selected unit range and calc max res from this
    if (source.first)
    {
        maxBits = source.first->getFilter().getExtractBits('D');
    }

    fill_resolution_combo(axisUi->combo_resolution, defaultMinBits, maxBits, selectedBits);
}
