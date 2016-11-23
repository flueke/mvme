#include "hist2ddialog.h"
#include "ui_hist2ddialog.h"

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
// Hist2DDialog
//
Hist2DDialog::Hist2DDialog(MVMEContext *context, Hist2D *histo, QWidget *parent)
    : QDialog(parent)
    , ui(new Ui::Hist2DDialog)
    , m_context(context)
    , m_histo(histo)
{
    m_xSource = QPair<DataFilterConfig *, int>(nullptr, -1);
    m_ySource = QPair<DataFilterConfig *, int>(nullptr, -1);

    ui->setupUi(this);

    auto validator = new NameValidator(context, histo, this);

    ui->le_name->setValidator(validator);

    if (m_histo)
    {
        auto histoConfig = qobject_cast<Hist2DConfig *>(m_context->getMappedObject(m_histo, QSL("ObjectToConfig")));

        if (histoConfig)
        {
            ui->le_name->setText(histoConfig->objectName());
            {
                auto filterConfig = m_context->getAnalysisConfig()->findChildById<DataFilterConfig *>(histoConfig->getXFilterId());
                auto address = histoConfig->getXFilterAddress();
                m_xSource = qMakePair(filterConfig, address);
            }

            {
                auto filterConfig = m_context->getAnalysisConfig()->findChildById<DataFilterConfig *>(histoConfig->getYFilterId());
                auto address = histoConfig->getYFilterAddress();
                m_ySource = qMakePair(filterConfig, address);
            }
        }
    }

    connect(ui->le_name, &QLineEdit::textChanged, this, [this](const QString &) {
        updateAndValidate();
    });

    static const int bitsMin =  9;
    static const int bitsMax = 13;

    for (int bits=bitsMin; bits<=bitsMax; ++bits)
    {
        int value = 1 << bits;
        QString text = QString("%1, %2 bit")
            .arg(value, 4)
            .arg(bits, 2);

        ui->comboXResolution->addItem(text, bits);
        ui->comboYResolution->addItem(text, bits);
    }

    if (m_histo)
    {
        ui->comboXResolution->setCurrentIndex(m_histo->getXBits() - bitsMin);
        ui->comboYResolution->setCurrentIndex(m_histo->getYBits() - bitsMin);
    }
    else
    {
        ui->comboXResolution->setCurrentIndex(1);
        ui->comboYResolution->setCurrentIndex(1);
    }

    updateAndValidate();
}

Hist2DDialog::~Hist2DDialog()
{
    delete ui;
}

QPair<Hist2D *, Hist2DConfig *> Hist2DDialog::getHistoAndConfig()
{
    u32 xBits = ui->comboXResolution->currentData().toInt();
    u32 yBits = ui->comboYResolution->currentData().toInt();
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

            histoConfig->setXFilterId(filter->getId());
            histoConfig->setXFilterAddress(address);
            histoConfig->setXBits(xBits);
            histoConfig->setProperty("xAxisTitle", title);
            histoConfig->setProperty("xAxisUnit", filter->getUnitString());
            histoConfig->setProperty("xAxisUnitMin", filter->getUnitMinValue());
            histoConfig->setProperty("xAxisUnitMax", filter->getUnitMaxValue());
        }

        // y axis
        {
            auto filter = m_ySource.first;
            auto address = m_ySource.second;
            auto title = filter->getAxisTitle();
            if (title.isEmpty())
                title = QString("%1/%2").arg(filter->objectName()).arg(address);

            histoConfig->setYFilterId(filter->getId());
            histoConfig->setYFilterAddress(address);
            histoConfig->setYBits(yBits);
            histoConfig->setProperty("yAxisTitle", title);
            histoConfig->setProperty("yAxisUnit", filter->getUnitString());
            histoConfig->setProperty("yAxisUnitMin", filter->getUnitMinValue());
            histoConfig->setProperty("yAxisUnitMax", filter->getUnitMaxValue());
        }
    }

    return qMakePair(m_histo, histoConfig);
}

void Hist2DDialog::on_pb_xSource_clicked()
{
    int eventIndex = -1;
    if (m_ySource.first)
    {
        eventIndex = m_context->getAnalysisConfig()->getEventAndModuleIndices(m_ySource.first).first;
    }

    SelectAxisSourceDialog dialog(m_context, eventIndex, this);

    if (dialog.exec() == QDialog::Accepted)
    {
        m_xSource = dialog.getAxisSource();
        updateAndValidate();
    }
}

void Hist2DDialog::on_pb_ySource_clicked()
{
    int eventIndex = -1;

    if (m_xSource.first)
    {
        eventIndex = m_context->getAnalysisConfig()->getEventAndModuleIndices(m_xSource.first).first;
    }

    SelectAxisSourceDialog dialog(m_context, eventIndex, this);
    if (dialog.exec() == QDialog::Accepted)
    {
        m_ySource = dialog.getAxisSource();
        updateAndValidate();
    }
}

void Hist2DDialog::on_pb_xClear_clicked()
{
    m_xSource = QPair<DataFilterConfig *, int>(nullptr, -1);
    updateAndValidate();
}

void Hist2DDialog::on_pb_yClear_clicked()
{
    m_ySource = QPair<DataFilterConfig *, int>(nullptr, -1);
    updateAndValidate();
}

void Hist2DDialog::updateAndValidate()
{
    ui->label_xSource->setText(QSL("<none selected>"));
    ui->label_ySource->setText(QSL("<none selected>"));

    if (m_xSource.first)
    {
        ui->label_xSource->setText(getFilterPath(m_context, m_xSource.first, m_xSource.second));
    }

    if (m_ySource.first)
    {
        ui->label_ySource->setText(getFilterPath(m_context, m_ySource.first, m_ySource.second));
    }

    bool valid = (m_xSource.first && m_ySource.first && ui->le_name->hasAcceptableInput());
    ui->buttonBox->button(QDialogButtonBox::Ok)->setEnabled(valid);
}
