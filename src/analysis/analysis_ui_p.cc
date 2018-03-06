/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2018 mesytec GmbH & Co. KG <info@mesytec.com>
 *
 * Author: Florian Lüke <f.lueke@mesytec.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 */
#include "a2_adapter.h"
#include "analysis_ui_p.h"
#include "analysis_util.h"
#include "data_extraction_widget.h"
#include "../globals.h"
#include "../histo_util.h"
#include "../vme_config.h"
#include "../mvme_context.h"
#include "../qt_util.h"
#include "../data_filter.h"

#include <array>
#include <limits>
#include <QAbstractItemModel>
#include <QButtonGroup>
#include <QDir>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QListWidget>
#include <QMessageBox>
#include <QRadioButton>
#include <QSignalMapper>
#include <QStackedWidget>
#include <QTextBrowser>

namespace
{
    QDoubleSpinBox *make_calibration_spinbox(const QString &specialValueText = QSL("not set"),
                                             QWidget *parent = nullptr)
    {
        auto result = new QDoubleSpinBox(parent);

        result->setDecimals(8);
        result->setMinimum(-1e20);
        result->setMaximum(+1e20);
        result->setValue(result->minimum());
        result->setSpecialValueText(specialValueText);

        return result;
    }
}

namespace analysis
{
//
// AddEditExtractorWidget
//

/** IMPORTANT: This constructor makes the Widget go into "add" mode. When
 * accepting the widget inputs it will call eventWidget->addSource(). */
AddEditExtractorWidget::AddEditExtractorWidget(SourcePtr srcPtr, ModuleConfig *mod, EventWidget *eventWidget)
    : AddEditExtractorWidget(srcPtr.get(), mod, eventWidget)
{
    m_srcPtr = srcPtr;
    setWindowTitle(QString("New  %1").arg(srcPtr->getDisplayName()));

    // Histogram generation and calibration
    m_gbGenHistograms = new QGroupBox(QSL("Generate Histograms"));
    m_gbGenHistograms->setCheckable(true);
    m_gbGenHistograms->setChecked(true);

    le_unit = new QLineEdit;

    spin_unitMin = new QDoubleSpinBox;
    spin_unitMin->setDecimals(8);
    spin_unitMin->setMinimum(-1e20);
    spin_unitMin->setMaximum(+1e20);
    spin_unitMin->setValue(0.0);

    spin_unitMax = new QDoubleSpinBox;
    spin_unitMax->setDecimals(8);
    spin_unitMax->setMinimum(-1e20);
    spin_unitMax->setMaximum(+1e20);
    spin_unitMax->setValue(1 << 16); // FIXME: find a better default value. Maybe input dependent (upperLimit)

    auto genHistogramsLayout = new QHBoxLayout(m_gbGenHistograms);
    genHistogramsLayout->setContentsMargins(0, 0, 0, 0);
    auto calibInfoFrame = new QFrame;
    genHistogramsLayout->addWidget(calibInfoFrame);
    auto calibInfoLayout = new QFormLayout(calibInfoFrame);
    calibInfoLayout->addRow(QSL("Unit Label"), le_unit);
    calibInfoLayout->addRow(QSL("Unit Min"), spin_unitMin);
    calibInfoLayout->addRow(QSL("Unit Max"), spin_unitMax);

    connect(m_gbGenHistograms, &QGroupBox::toggled, this, [calibInfoFrame](bool checked) {
        calibInfoFrame->setEnabled(checked);
    });

    m_optionsLayout->insertRow(m_optionsLayout->rowCount() - 1, m_gbGenHistograms);

    // Load data from the first template into the gui
    applyTemplate(0);
}

/** IMPORTANT: This constructor makes the Widget go into "edit" mode. When
 * accepting the widget inputs it will call eventWidget->sourceEdited(). */
AddEditExtractorWidget::AddEditExtractorWidget(SourceInterface *src, ModuleConfig *module, EventWidget *eventWidget)
    : QDialog(eventWidget)
    , m_src(src)
    , m_module(module)
    , m_eventWidget(eventWidget)
    , m_gbGenHistograms(nullptr)
{
    setWindowTitle(QString("Edit %1").arg(m_src->getDisplayName()));
    add_widget_close_action(this);

    m_defaultExtractors = get_default_data_extractors(module->getModuleMeta().typeName);

    auto loadTemplateButton = new QPushButton(QIcon(QSL(":/document_import.png")), QSL("Load Filter Template"));
    auto loadTemplateLayout = new QHBoxLayout;
    loadTemplateLayout->setContentsMargins(0, 0, 0, 0);
    loadTemplateLayout->addWidget(loadTemplateButton);
    loadTemplateLayout->addStretch();

    connect(loadTemplateButton, &QPushButton::clicked, this, &AddEditExtractorWidget::runLoadTemplateDialog);

    auto extractor = qobject_cast<Extractor *>(src);
    Q_ASSERT(extractor);
    Q_ASSERT(module);

    le_name = new QLineEdit;
    m_filterEditor = new DataExtractionEditor(extractor->getFilter().getSubFilters());
    m_filterEditor->setMinimumHeight(125);
    m_filterEditor->setMinimumWidth(550);

    m_spinCompletionCount = new QSpinBox;
    m_spinCompletionCount->setMinimum(1);
    m_spinCompletionCount->setMaximum(std::numeric_limits<int>::max());

    if (!extractor->objectName().isEmpty())
    {
        le_name->setText(QString("%1").arg(extractor->objectName()));
    }

    m_spinCompletionCount->setValue(extractor->getRequiredCompletionCount());

    m_optionsLayout = new QFormLayout;
    m_optionsLayout->addRow(QSL("Name"), le_name);
    m_optionsLayout->addRow(QSL("Required Completion Count"), m_spinCompletionCount);

    if (m_defaultExtractors.size())
    {
        m_optionsLayout->addRow(loadTemplateLayout);
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddEditExtractorWidget::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &AddEditExtractorWidget::reject);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonBox);

    auto layout = new QVBoxLayout(this);

    layout->addWidget(m_filterEditor);
    layout->addLayout(m_optionsLayout);
    layout->addLayout(buttonBoxLayout);

    layout->setStretch(0, 1);
}

void AddEditExtractorWidget::runLoadTemplateDialog()
{
    auto templateList = new QListWidget;

    for (auto &ex: m_defaultExtractors)
    {
        templateList->addItem(ex->objectName());
    }

    QDialog dialog(this);
    dialog.setWindowTitle(QSL("Load Extraction Filter Template"));
    auto bb = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    bb->button(QDialogButtonBox::Ok)->setDefault(true);
    bb->button(QDialogButtonBox::Ok)->setText(QSL("Load"));
    connect(bb, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
    connect(bb, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);

    connect(templateList, &QListWidget::itemDoubleClicked, &dialog, &QDialog::accept);

    auto layout = new QVBoxLayout(&dialog);
    layout->addWidget(templateList);
    layout->addWidget(bb);

    if (dialog.exec() == QDialog::Accepted)
    {
        applyTemplate(templateList->currentRow());
    }
}

/** Loads the template with the given \c index into the GUI.
 *
 * The index is an index into a vector of Extractor instances obtained from
 * get_default_data_extractors() cached in m_defaultExtractors.
 */
void AddEditExtractorWidget::applyTemplate(int index)
{
    if (0 <= index && index < m_defaultExtractors.size())
    {
        auto tmpl = m_defaultExtractors[index];
        m_filterEditor->setSubFilters(tmpl->getFilter().getSubFilters());
        QString name = m_module->objectName() + QSL(".") + tmpl->objectName().section('.', 0, -1);
        le_name->setText(name);
        m_spinCompletionCount->setValue(tmpl->getRequiredCompletionCount());
    }
}

void AddEditExtractorWidget::accept()
{
    qDebug() << __PRETTY_FUNCTION__;

    AnalysisPauser pauser(m_eventWidget->getContext());

    auto extractor = qobject_cast<Extractor *>(m_src);

    extractor->setObjectName(le_name->text());
    m_filterEditor->apply();
    extractor->getFilter().setSubFilters(m_filterEditor->m_subFilters);
    extractor->setRequiredCompletionCount(m_spinCompletionCount->value());

    if (m_srcPtr)
    {
        m_eventWidget->addSource(m_srcPtr, m_module, m_gbGenHistograms->isChecked(),
                                 le_unit->text(), spin_unitMin->value(), spin_unitMax->value());
    }
    else
    {
        m_eventWidget->sourceEdited(m_src);
    }

    m_eventWidget->uniqueWidgetCloses();
    QDialog::accept();
}

void AddEditExtractorWidget::reject()
{
    qDebug() << __PRETTY_FUNCTION__;
    m_eventWidget->uniqueWidgetCloses();
    QDialog::reject();
}

//
// ListFilterExtractorDialog
//

/* Parts
 * Left:
 *   Modulename label
 *   Filter list or table
 *   Input words used label
 *   Filter operations: buttons or tableview interaction
 *   clone (copy) operation
 *
 *  Right:
 *    ListFilter name label
 *    Repetitions
 *    Combine options:
 *      word size
 *      word count
 *      swap word order
 *
 *    Filter fields:
 *     repetition index
 *     two 32 bit filter inputs
 *
 *
 * Right side embedded into a QStackedWidget.
 *
 * Have to keep listview and stackedwidget contents in sync.
 *
 * Edit case:
 * - Edit an existing filter.
 * - Add a new filter. The module may already contain listfilters or this may
 *   be the very first listfilter added to the module.
 *
 * -> If there are no filters to display always create a new unnamed default
 *  filter.  On hitting apply add that filter to the analysis and repopulate
 *  the widget -> now the filter contenst will be reloaded from the analysis
 *  and everythin is in sync again.
 *  When deleting the last filter either close the dialog or go back to
 *  creating an unnamed default filter.
 *
 * Maybe it's better to not populate from the analysis at all but instead get a
 * list of filters from the analysis and set it on the dialog. Later on set
 * that list on the analysis.
 *
 * What are we editing? The whole list of ListFilterExtractor for a certain
 * module as they all interact.
 *
 * If there are no filters we start out with a default filter for the module.
 *
 * TODO: Get the list of filters for the module from the
 *   Walk SourceEntry vector and filter by moduleId and SourcePtr type (must be ListFilterExtractor).
 *   => vector of shared ptr to ListFilterExtractor
 *
 *   Existing ones are directly edited as setExtractor() is used to update the a2::ListFilterExtractor.
 *   New ones have to be added to the analysis in the correct order.
 *   Moving operators changes the order so the SourceEntry vector of the
 *   analysis has to be updated. How to do this?
 *
 */

/* Inputs and logic for for editing one ListFilter. */
struct ListFilterEditor
{
    QWidget *widget;

    QLabel *label_addressBits,
           *label_dataBits,
           *label_outputSize,
           *label_combinedBits;

    QSpinBox *spin_repetitions,
             *spin_wordCount;

    QLineEdit *le_name,
              *filter_lowWord,
              *filter_highWord,
              *filter_repIndex;

    QComboBox *combo_wordSize;

    QCheckBox *cb_swapWords;
};

static a2::data_filter::ListFilter listfilter_editor_make_a2_listfilter(ListFilterEditor e)
{
    using namespace a2::data_filter;

    std::vector<std::string> filters =
    {
        e.filter_lowWord->text().toStdString(),
        e.filter_highWord->text().toStdString(),
    };

    ListFilter::Flag flags = e.combo_wordSize->currentData().toUInt();

    if (e.cb_swapWords->isChecked())
    {
        flags |= ListFilter::ReverseCombine;
    }

    return make_listfilter(flags, e.spin_wordCount->value(), filters);
}

static a2::ListFilterExtractor listfilter_editor_make_a2_extractor(ListFilterEditor e)
{
    using namespace a2::data_filter;

    auto listFilter = listfilter_editor_make_a2_listfilter(e);

    auto repFilter = make_filter(e.filter_repIndex->text().toStdString());

    a2::ListFilterExtractor ex_a2 = a2::make_listfilter_extractor(
        listFilter,
        repFilter,
        e.spin_repetitions->value(),
        0);

    return ex_a2;
}

static void listfilter_editor_update(ListFilterEditor e)
{
    auto ex = listfilter_editor_make_a2_extractor(e);
    auto addressBits = get_address_bits(&ex);
    auto addressCount = get_address_count(&ex);
    auto dataBits = get_extract_bits(&ex.listFilter, a2::data_filter::MultiWordFilter::CacheD);
    size_t combinedBits = ((ex.listFilter.flags & a2::data_filter::ListFilter::WordSize32) ? 32 : 16) * ex.listFilter.wordCount;

    e.label_addressBits->setText(QString::number(addressBits));
    e.label_outputSize->setText(QString::number(addressCount));
    e.label_dataBits->setText(QString::number(dataBits));
    e.label_combinedBits->setText(QString::number(combinedBits));
}

static ListFilterEditor make_listfilter_editor(QWidget *parent = nullptr)
{
    using a2::data_filter::ListFilter;

    ListFilterEditor e = {};

    e.widget = new QWidget(parent);
    e.le_name = new QLineEdit;
    e.label_addressBits = new QLabel;
    e.label_dataBits = new QLabel;
    e.label_outputSize = new QLabel;
    e.label_combinedBits = new QLabel;
    e.spin_repetitions = new QSpinBox;
    e.spin_wordCount = new QSpinBox;
    e.filter_lowWord = makeFilterEdit();
    e.filter_highWord = makeFilterEdit();
    e.filter_repIndex = makeFilterEdit();
    e.combo_wordSize = new QComboBox;
    e.cb_swapWords = new QCheckBox;

    e.spin_repetitions->setMinimum(1);
    e.spin_repetitions->setMaximum(std::numeric_limits<u8>::max());

    e.combo_wordSize->addItem("16 bit", ListFilter::NoFlag);
    e.combo_wordSize->addItem("32 bit", ListFilter::WordSize32);

    e.spin_wordCount->setMinimum(1);

    // word size handling
    auto on_wordSize_selected = [e] (int index)
    {
        switch (static_cast<ListFilter::Flag>(e.combo_wordSize->itemData(index).toInt()))
        {
            case ListFilter::NoFlag:
                e.spin_wordCount->setMaximum(4);
                break;

            case ListFilter::WordSize32:
                e.spin_wordCount->setMaximum(2);
                break;
        }
    };

    QObject::connect(e.combo_wordSize, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                     e.widget, on_wordSize_selected);

    on_wordSize_selected(0);

    // filter edits
    e.filter_lowWord->setText(generate_pretty_filter_string(32, 'X'));
    e.filter_highWord->setText(generate_pretty_filter_string(32, 'X'));
    e.filter_repIndex->setInputMask("                              NNNN NNNN");
    e.filter_repIndex->setText(     "                              XXXX XXXX");

    auto update_editor = [e]() { listfilter_editor_update(e); };

    // bit count and output size labels
    //repetitions and wordCount and all filter edits
    QObject::connect(e.spin_repetitions, static_cast<void (QSpinBox::*) (int)>(&QSpinBox::valueChanged),
                     e.widget, update_editor);
    QObject::connect(e.spin_wordCount, static_cast<void (QSpinBox::*) (int)>(&QSpinBox::valueChanged),
                     e.widget, update_editor);
    QObject::connect(e.combo_wordSize, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                     e.widget, update_editor);

    QObject::connect(e.filter_lowWord, &QLineEdit::textChanged, e.widget, update_editor);
    QObject::connect(e.filter_highWord, &QLineEdit::textChanged, e.widget, update_editor);
    QObject::connect(e.filter_repIndex, &QLineEdit::textChanged, e.widget, update_editor);

    // layout
    auto layout = new QFormLayout(e.widget);

    layout->addRow("Name", e.le_name);
    layout->addRow("Repetitions", e.spin_repetitions);

    {
        auto gb_input = new QGroupBox("Input");
        auto layout_input = new QFormLayout(gb_input);

        layout_input->addRow("Word size", e.combo_wordSize);
        layout_input->addRow("Words to combine", e.spin_wordCount);
        layout_input->addRow("Reverse combine", e.cb_swapWords);

        layout->addRow(gb_input);
    }

    {
        auto gb_extraction = new QGroupBox("Data Extraction");
        auto layout_extraction = new QFormLayout(gb_extraction);

        layout_extraction->addRow("Repetition", e.filter_repIndex);
        layout_extraction->addRow("High word", e.filter_highWord);
        layout_extraction->addRow("Low word", e.filter_lowWord);

        layout->addRow(gb_extraction);
    }

    {
        auto gb_info = new QGroupBox("Info");
        auto layout_info = new QFormLayout(gb_info);

        layout_info->addRow("Bits from combine step:", e.label_combinedBits);
        layout_info->addRow("Extracted data bits:", e.label_dataBits);
        layout_info->addRow("Extracted address bits:", e.label_addressBits);
        layout_info->addRow("Output vector size:", e.label_outputSize);

        layout->addRow(gb_info);
    }

    return e;
}

static void listfilter_editor_load_from_extractor(ListFilterEditor e, const ListFilterExtractor *ex)
{
    using a2::data_filter::ListFilter;

    e.le_name->setText(ex->objectName());

    auto ex_a2 = ex->getExtractor();
    auto listfilter = ex_a2.listFilter;

    e.spin_repetitions->setValue(ex_a2.repetitions);
    e.combo_wordSize->setCurrentIndex(listfilter.flags & ListFilter::WordSize32);
    e.spin_wordCount->setValue(listfilter.wordCount);
    e.cb_swapWords->setChecked(listfilter.flags & ListFilter::ReverseCombine);

    auto lo  = QString::fromStdString(to_string(listfilter.extractionFilter.filters[0]));
    auto hi  = QString::fromStdString(to_string(listfilter.extractionFilter.filters[1]));
    auto rep = QString::fromStdString(to_string(ex_a2.repetitionAddressFilter)).right(8);

#if 1
    qDebug() << "lo =" << lo
        << "\nhi =" << hi
        << "\nrep =" << rep;
#endif

    e.filter_lowWord->setText(lo);
    e.filter_highWord->setText(hi);
    e.filter_repIndex->setText(rep);

    listfilter_editor_update(e);
}

static void listfilter_editor_save_to_extractor(ListFilterEditor e, ListFilterExtractor *ex)
{
    using namespace a2::data_filter;

    auto ex_a2 = listfilter_editor_make_a2_extractor(e);

    ex->setObjectName(e.le_name->text());
    ex->setExtractor(ex_a2);
}

struct ListFilterListWidgetUi
{
    QWidget *widget;

    QListWidget *listWidget;

    QPushButton *pb_addFilter,
                *pb_removeFilter,
                *pb_cloneFilter;

    QLabel *label_moduleName,
           *label_totalWordsUsed;
};

static ListFilterListWidgetUi make_listfilter_list_ui(QWidget *parent = nullptr)
{
    ListFilterListWidgetUi ui = {};

    ui.widget = new QWidget(parent);
    ui.listWidget = new QListWidget;
    ui.pb_addFilter = new QPushButton("Add Filter");
    ui.pb_removeFilter = new QPushButton("Remove Filter");
    ui.pb_cloneFilter = new QPushButton("Clone Filter");
    ui.label_moduleName = new QLabel;
    ui.label_totalWordsUsed = new QLabel;

    // layout

    QSizePolicy pol(QSizePolicy::Preferred, QSizePolicy::Preferred);
    pol.setVerticalStretch(1);
    ui.listWidget->setSizePolicy(pol);
    ui.listWidget->setSelectionMode(QAbstractItemView::SingleSelection);
    ui.listWidget->setDragEnabled(true);
    ui.listWidget->viewport()->setAcceptDrops(true);
    ui.listWidget->setDropIndicatorShown(true);
    ui.listWidget->setDragDropMode(QAbstractItemView::InternalMove);

    auto widgetLayout = new QFormLayout(ui.widget);

    widgetLayout->addRow("Module name", ui.label_moduleName);
    widgetLayout->addRow(ui.listWidget);
    widgetLayout->addRow("Total input words used", ui.label_totalWordsUsed);
    {
        auto l = new QHBoxLayout;
        l->addWidget(ui.pb_addFilter);
        l->addWidget(ui.pb_removeFilter);
        l->addWidget(ui.pb_cloneFilter);

        widgetLayout->addRow(l);
    }

    ui.pb_removeFilter->setEnabled(false);
    ui.pb_cloneFilter->setEnabled(false);

    return ui;
}

struct ListFilterExtractorDialog::ListFilterExtractorDialogPrivate
{
    ModuleConfig *m_module;
    analysis::Analysis *m_analysis;
    MVMEContext *m_context;
    QVector<ListFilterExtractorPtr> m_extractors;
    QVector<ListFilterEditor> m_filterEditors;

    QStackedWidget *m_editorStack;
    ListFilterListWidgetUi listWidgetUi;

    QSignalMapper m_nameChangedMapper;
};

ListFilterExtractorDialog::ListFilterExtractorDialog(ModuleConfig *mod, analysis::Analysis *analysis,
                                                     MVMEContext *context, QWidget *parent)
    : QDialog(parent)
    , m_d(std::make_unique<ListFilterExtractorDialogPrivate>())
{
    m_d->m_module = mod;
    m_d->m_analysis = analysis;
    m_d->m_context = context;
    m_d->listWidgetUi = make_listfilter_list_ui();
    m_d->m_editorStack = new QStackedWidget;

    // left filter list layout
    auto filterListLayout = new QVBoxLayout;
    filterListLayout->addWidget(m_d->listWidgetUi.widget);

    auto editorLayout = new QVBoxLayout;
    editorLayout->addWidget(m_d->m_editorStack);
    editorLayout->addStretch(1);

    // contents layout: filter list on the left, stack of filter editors to the right
    auto contentsLayout = new QHBoxLayout;
    contentsLayout->addLayout(filterListLayout);
    contentsLayout->addLayout(editorLayout);
    contentsLayout->setStretch(1, 1);

    // buttonbox
    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Apply | QDialogButtonBox::Cancel);
    buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);

    // outer widget layout: list and edit widgets top, buttonbox bottom
    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->addLayout(contentsLayout);
    widgetLayout->setStretch(0, 1);
    widgetLayout->addWidget(buttonBox);


    //
    // gui state changes and interactions
    //


    setWindowTitle(QSL("ListFilter Editor"));

    connect(buttonBox, &QDialogButtonBox::accepted, this, &ListFilterExtractorDialog::accept);
    connect(buttonBox, &QDialogButtonBox::rejected, this, &ListFilterExtractorDialog::reject);
    connect(buttonBox, &QDialogButtonBox::clicked, this, [this, buttonBox](QAbstractButton *button) {
        if (buttonBox->buttonRole(button) == QDialogButtonBox::ApplyRole) { apply(); }
    });
    auto on_listWidgetUi_currentRowChanged = [this](int index)
    {
        m_d->listWidgetUi.pb_removeFilter->setEnabled(index >= 0 && m_d->m_extractors.size() > 1);
        m_d->listWidgetUi.pb_cloneFilter->setEnabled(index >= 0 && m_d->m_extractors.size() > 0);
        m_d->m_editorStack->setCurrentIndex(index);
    };

    connect(m_d->listWidgetUi.listWidget, &QListWidget::currentRowChanged,
            this, on_listWidgetUi_currentRowChanged);

    connect(m_d->listWidgetUi.pb_addFilter, &QPushButton::clicked,
            this, &ListFilterExtractorDialog::newFilter);

    connect(m_d->listWidgetUi.pb_removeFilter, &QPushButton::clicked,
            this, &ListFilterExtractorDialog::removeFilter);

    connect(m_d->listWidgetUi.pb_cloneFilter, &QPushButton::clicked,
            this, &ListFilterExtractorDialog::cloneFilter);

    connect(m_d->listWidgetUi.listWidget->model(), &QAbstractItemModel::rowsMoved,
            this, [this](const QModelIndex &parent, int srcIdx, int endIdx,
                         const QModelIndex &dest, int dstIdx) {

                qDebug() << "orig: srcIdx =" << srcIdx << ", dstIdx =" << dstIdx;

                Q_ASSERT(endIdx - srcIdx == 0);
                Q_ASSERT(srcIdx != dstIdx);

                auto ex = m_d->m_extractors[srcIdx];
                auto fe = m_d->m_filterEditors[srcIdx];

                m_d->m_extractors.remove(srcIdx);
                m_d->m_filterEditors.remove(srcIdx);

                if (dstIdx > srcIdx)
                    dstIdx -= 1;

                if (dstIdx > m_d->m_extractors.size())
                    dstIdx = m_d->m_extractors.size();

                m_d->m_extractors.insert(dstIdx, ex);
                m_d->m_filterEditors.insert(dstIdx, fe);

                // Instead of fiddling with the editorStack, trying to figure
                // out which widget has to move where this code just empties
                // the stack and repopulates it using the existing ListFilterEditors.

                while (m_d->m_editorStack->count())
                {
                    auto w = m_d->m_editorStack->widget(0);
                    m_d->m_editorStack->removeWidget(w);
                }

                for (const auto &editor: m_d->m_filterEditors)
                {
                    m_d->m_editorStack->addWidget(editor.widget);
                }

                auto w = m_d->m_editorStack->widget(dstIdx);
                m_d->m_editorStack->setCurrentWidget(w);

                Q_ASSERT(m_d->m_editorStack->count() == m_d->m_extractors.size());
                Q_ASSERT(m_d->m_editorStack->count() == m_d->m_filterEditors.size());
     });


    auto on_nameChanged = [this](int index)
    {
        qDebug() << __PRETTY_FUNCTION__ << index;
        auto item = m_d->listWidgetUi.listWidget->item(index);
        if (item && 0 <= index && index < m_d->m_filterEditors.size())
        {
            auto editor = m_d->m_filterEditors.at(index);
            item->setText(editor.le_name->text());
        }
    };

    connect(&m_d->m_nameChangedMapper, static_cast<void (QSignalMapper::*) (int index)>(&QSignalMapper::mapped),
            this, on_nameChanged);

    repopulate();
}

ListFilterExtractorDialog::~ListFilterExtractorDialog()
{
}

void ListFilterExtractorDialog::repopulate()
{
    m_d->m_extractors = m_d->m_analysis->getListFilterExtractors(m_d->m_module);

    if (m_d->m_extractors.isEmpty())
        newFilter();

    int oldRow = m_d->listWidgetUi.listWidget->currentRow();

    // clear the stacked editor widgets
    while (m_d->m_editorStack->count())
    {
        auto w = m_d->m_editorStack->widget(0);
        m_d->m_editorStack->removeWidget(w);
        w->deleteLater();
    }

    // clear the left list widget
    while (auto lw = m_d->listWidgetUi.listWidget->takeItem(0))
    {
        delete lw;
    }

    m_d->m_filterEditors.clear();

    // populate
    for (auto ex: m_d->m_extractors)
    {
        addFilterToUi(ex);
    }

    // restore selection if possible
    if (0 <= oldRow && oldRow < m_d->m_extractors.size())
    {
        m_d->listWidgetUi.listWidget->setCurrentRow(oldRow);
    }
    else
    {
        m_d->listWidgetUi.listWidget->setCurrentRow(0);
    }

    m_d->listWidgetUi.label_moduleName->setText(m_d->m_module->objectName());
}

int ListFilterExtractorDialog::addFilterToUi(const ListFilterExtractorPtr &ex)
{
    auto editor = make_listfilter_editor();
    listfilter_editor_load_from_extractor(editor, ex.get());
    m_d->m_filterEditors.push_back(editor);
    m_d->m_editorStack->addWidget(editor.widget);
    m_d->listWidgetUi.listWidget->addItem(ex->objectName());

    connect(editor.le_name, &QLineEdit::textChanged,
            &m_d->m_nameChangedMapper, static_cast<void (QSignalMapper::*) ()>(&QSignalMapper::map));

    m_d->m_nameChangedMapper.setMapping(editor.le_name, m_d->m_filterEditors.size() - 1);

    connect(editor.spin_wordCount, static_cast<void (QSpinBox::*) (int)>(&QSpinBox::valueChanged),
            this, &ListFilterExtractorDialog::updateWordCount);

    connect(editor.spin_repetitions, static_cast<void (QSpinBox::*) (int)>(&QSpinBox::valueChanged),
            this, &ListFilterExtractorDialog::updateWordCount);

    updateWordCount();

    return m_d->listWidgetUi.listWidget->count() - 1;
}

void ListFilterExtractorDialog::updateWordCount()
{
    qDebug() << __PRETTY_FUNCTION__;

    size_t wordCount = 0;

    for (const auto &e: m_d->m_filterEditors)
    {
        auto ex = listfilter_editor_make_a2_extractor(e);

        wordCount += ex.repetitions * ex.listFilter.wordCount;
    }

    m_d->listWidgetUi.label_totalWordsUsed->setText(QString::number(wordCount));
}

void ListFilterExtractorDialog::accept()
{
    qDebug() << __PRETTY_FUNCTION__;
    apply();
    QDialog::accept();
}

void ListFilterExtractorDialog::reject()
{
    qDebug() << __PRETTY_FUNCTION__;
    QDialog::reject();
}

void ListFilterExtractorDialog::apply()
{
    qDebug() << __PRETTY_FUNCTION__;

    Q_ASSERT(m_d->m_extractors.size() == m_d->listWidgetUi.listWidget->count());
    Q_ASSERT(m_d->m_extractors.size() == m_d->m_editorStack->count());
    Q_ASSERT(m_d->m_extractors.size() == m_d->m_filterEditors.size());

    for (int i = 0; i < m_d->m_extractors.size(); i++)
    {
        auto ex = m_d->m_extractors.at(i);
        auto filterEditor = m_d->m_filterEditors.at(i);
        listfilter_editor_save_to_extractor(filterEditor, ex.get());
    }

    {
        AnalysisPauser pauser(m_d->m_context);
        m_d->m_analysis->setListFilterExtractors(m_d->m_module, m_d->m_extractors);
    }

    repopulate();

    emit applied();
}

void ListFilterExtractorDialog::newFilter()
{
    auto ex = std::make_shared<ListFilterExtractor>();
    ex->setObjectName(QSL("new filter"));

    auto listFilter = a2::data_filter::make_listfilter(
        a2::data_filter::ListFilter::NoFlag, 1, {
            "XXXX XXXX XXXX XXXX XXXX XXXX AAAA AAAA",
            "XXXX XXXX XXXX XXXX DDDD DDDD DDDD DDDD" });

    auto repFilter = a2::data_filter::make_filter("XXXX XXXX");

    auto ex_a2 = a2::make_listfilter_extractor(listFilter, repFilter, 1, 0);

    ex->setExtractor(ex_a2);

    m_d->m_extractors.push_back(ex);
    int idx = addFilterToUi(ex);
    m_d->listWidgetUi.listWidget->setCurrentRow(idx);
}

void ListFilterExtractorDialog::removeFilter()
{
    int index = m_d->listWidgetUi.listWidget->currentRow();
    Q_ASSERT(0 <= index && index < m_d->m_extractors.size());

    m_d->m_extractors.removeAt(index);
    delete m_d->listWidgetUi.listWidget->takeItem(index);
    auto widget = m_d->m_editorStack->widget(index);
    m_d->m_editorStack->removeWidget(widget);
    widget->deleteLater();
    m_d->m_filterEditors.removeAt(index);
}

void ListFilterExtractorDialog::cloneFilter()
{
    int index = m_d->listWidgetUi.listWidget->currentRow();
    Q_ASSERT(0 <= index && index < m_d->m_extractors.size());

    auto clone = std::make_shared<ListFilterExtractor>();
    listfilter_editor_save_to_extractor(m_d->m_filterEditors[index], clone.get());
    clone->setObjectName(clone->objectName() + " copy");
    m_d->m_extractors.push_back(clone);
    int idx = addFilterToUi(clone);
    m_d->listWidgetUi.listWidget->setCurrentRow(idx);
}

void ListFilterExtractorDialog::editSource(const SourcePtr &src)
{
    qDebug() << __PRETTY_FUNCTION__ << src.get();
    if (auto lfe = std::dynamic_pointer_cast<ListFilterExtractor>(src))
    {
        int idx = m_d->m_extractors.indexOf(lfe);
        if (0 <= idx && idx < m_d->m_extractors.size())
        {
            m_d->listWidgetUi.listWidget->setCurrentRow(idx);
        }
    }
}

QVector<ListFilterExtractorPtr> ListFilterExtractorDialog::getExtractors() const
{
    return m_d->m_extractors;
}

//
// AddEditOperatorWidget
//

/** IMPORTANT: This constructor makes the Widget go into "add" mode. When
 * accepted it will call eventWidget->addOperator()! */
AddEditOperatorWidget::AddEditOperatorWidget(OperatorPtr opPtr, s32 userLevel, EventWidget *eventWidget)
    : AddEditOperatorWidget(opPtr.get(), userLevel, eventWidget)
{
    m_opPtr = opPtr;
    setWindowTitle(QString("New  %1").arg(opPtr->getDisplayName()));

    // Creating a new operator. Override the setting of setNameEdited by the
    // constructor below.
    m_opConfigWidget->setNameEdited(false);
}

/** IMPORTANT: This constructor makes the Widget go into "edit" mode. When
 * accepted it will call eventWidget->operatorEdited()! */
AddEditOperatorWidget::AddEditOperatorWidget(OperatorInterface *op, s32 userLevel, EventWidget *eventWidget)
    : QDialog(eventWidget)
    , m_op(op)
    , m_userLevel(userLevel)
    , m_eventWidget(eventWidget)
    , m_opConfigWidget(nullptr)
{
    // Note: refactor this into some factory or table lookup based on
    // qmetaobject if there are more operator specific widgets to handle
    if (auto rms = qobject_cast<RateMonitorSink *>(op))
    {
        m_opConfigWidget = new RateMonitorConfigWidget(rms, userLevel, this);
    }
    else
    {
        m_opConfigWidget = new OperatorConfigurationWidget(op, userLevel, this);
    }

    setWindowTitle(QString("Edit %1").arg(m_op->getDisplayName()));
    add_widget_close_action(this);

    // We're editing an operator so we assume the name has been specified by the user.
    m_opConfigWidget->setNameEdited(true);

    const s32 slotCount = m_op->getNumberOfSlots();

    for (s32 slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        Slot *slot = m_op->getSlot(slotIndex);
        // Record the current slot input
        m_slotBackups.push_back({slot->inputPipe, slot->paramIndex});
    }

    auto slotFrame = new QFrame;
    auto slotGrid = new QGridLayout(slotFrame);
    m_slotGrid = slotGrid;
    slotGrid->setContentsMargins(0, 0, 0, 0);

    if (slotCount == 1 && !op->hasVariableNumberOfSlots())
    {
        slotGrid->setColumnStretch(0, 1);
        slotGrid->setColumnStretch(1, 0);
    }

    if (slotCount > 1 || op->hasVariableNumberOfSlots())
    {
        slotGrid->setColumnStretch(0, 0);
        slotGrid->setColumnStretch(1, 1);
        slotGrid->setColumnStretch(2, 0);
    }

    auto slotGroupBox = new QGroupBox(slotCount > 1 ? QSL("Inputs") : QSL("Input"));
    auto slotGroupBoxLayout = new QGridLayout(slotGroupBox);
    slotGroupBoxLayout->setContentsMargins(2, 2, 2, 2);
    slotGroupBoxLayout->addWidget(slotFrame, 0, 0, 1, 2);

    if (op->hasVariableNumberOfSlots())
    {
        m_addSlotButton = new QPushButton(QIcon(QSL(":/list_add.png")), QString());
        m_addSlotButton->setToolTip(QSL("Add input"));

        m_removeSlotButton = new QPushButton(QIcon(QSL(":/list_remove.png")), QString());
        m_removeSlotButton->setToolTip(QSL("Remove last input"));
        m_removeSlotButton->setEnabled(m_op->getNumberOfSlots() > 1);

        connect(m_addSlotButton, &QPushButton::clicked, this, [this] () {
            m_op->addSlot();
            repopulateSlotGrid();
            inputSelected(-1);
            m_removeSlotButton->setEnabled(m_op->getNumberOfSlots() > 1);
        });

        connect(m_removeSlotButton, &QPushButton::clicked, this, [this] () {
            if (m_op->getNumberOfSlots() > 1)
            {
                AnalysisPauser pauser(m_eventWidget->getContext());
                m_op->removeLastSlot();
                do_beginRun_forward(m_op);
                repopulateSlotGrid();
                inputSelected(-1);
            }
            m_removeSlotButton->setEnabled(m_op->getNumberOfSlots() > 1);
        });

        auto buttonLayout = new QHBoxLayout;
        buttonLayout->setContentsMargins(0, 0, 0, 0);
        buttonLayout->addStretch();
        buttonLayout->addWidget(m_addSlotButton);
        buttonLayout->addWidget(m_removeSlotButton);
        slotGroupBoxLayout->addLayout(buttonLayout, 1, 0, 1, 2);
    }

    m_buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
    m_buttonBox->button(QDialogButtonBox::Ok)->setDefault(true);
    connect(m_buttonBox, &QDialogButtonBox::accepted, this, &AddEditOperatorWidget::accept);
    connect(m_buttonBox, &QDialogButtonBox::rejected, this, &AddEditOperatorWidget::reject);
    auto buttonBoxLayout = new QVBoxLayout;
    buttonBoxLayout->addStretch();
    buttonBoxLayout->addWidget(m_buttonBox);

    auto layout = new QGridLayout(this);
    //layout->setContentsMargins(2, 2, 2, 2);

    s32 row = 0;
    // row, col, rowSpan, colSpan
    layout->addWidget(slotGroupBox, row++, 0);
    layout->addWidget(m_opConfigWidget, row++, 0, 1, 2);
    layout->addLayout(buttonBoxLayout, row++, 0);

    layout->setRowStretch(0, 0);
    layout->setRowStretch(1, 1);

    // The widget is complete, now populate the slot grid.
    repopulateSlotGrid();
}

void AddEditOperatorWidget::repopulateSlotGrid()
{
    // Clear the grid and the select buttons
    {
        while (QLayoutItem *child = m_slotGrid->takeAt(0))
        {
            if (auto widget = child->widget())
                delete widget;
            delete child;
        }

        Q_ASSERT(m_slotGrid->count() == 0);

        m_selectButtons.clear();
    }

    const s32 slotCount = m_op->getNumberOfSlots();
    s32 row = 0;
    s32 userLevel = m_userLevel;

    for (s32 slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        Slot *slot = m_op->getSlot(slotIndex);

        auto selectButton = new QPushButton(QSL("<select>"));
        selectButton->setCheckable(true);
        selectButton->setMouseTracking(true);
        selectButton->installEventFilter(this);
        m_selectButtons.push_back(selectButton);

        connect(selectButton, &QPushButton::toggled, this, [this, slot, slotIndex, userLevel](bool checked) {
            // Cancel any previous input selection. Has no effect if no input selection was active.
            m_eventWidget->endSelectInput();

            if (checked)
            {
                for (s32 buttonIndex = 0; buttonIndex < m_selectButtons.size(); ++buttonIndex)
                {
                    if (buttonIndex != slotIndex)
                    {
                        m_selectButtons[buttonIndex]->setChecked(false);
                    }
                }

                /* Tell the EventWidget that we want input for the current slot.
                 * The lambda is the callback for the EventWidget. This means
                 * inputSelected() will be called with the current slotIndex
                 * once input selection is complete. */
                m_eventWidget->selectInputFor(slot, userLevel, [this, slot, slotIndex] () {
                    // The assumption is that the analysis has been paused by
                    // the EventWidget.
                    Q_ASSERT(!m_eventWidget->getContext()->isAnalysisRunning());

                    // Update the slots source operator and all dependents
                    do_beginRun_forward(slot->parentOperator);
                    this->inputSelected(slotIndex);
                });
            }

            m_inputSelectActive = checked;
        });

        auto clearButton  = new QPushButton(QIcon(":/dialog-close.png"), QString());

        connect(clearButton, &QPushButton::clicked, this, [this, slot, slotIndex]() {
            // End any active input selection if any of the clear buttons is
            // clicked. Seems to make the most sense in the UI.
            m_eventWidget->endSelectInput();

            for (auto button: m_selectButtons)
            {
                button->setChecked(false);
            }

            AnalysisPauser pauser(m_eventWidget->getContext());
            // Clear the slot
            slot->disconnectPipe();
            // Update the current select button to reflect the change
            m_selectButtons[slotIndex]->setText(QSL("<select>"));
            // Disable ok button as there's now at least one unset input
            m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(false);
            m_opConfigWidget->inputSelected(slotIndex);
        });

        s32 col = 0;
        if (slotCount > 1 || m_op->hasVariableNumberOfSlots())
        {
            m_slotGrid->addWidget(new QLabel(slot->name), row, col++);
        }

        m_slotGrid->addWidget(selectButton, row, col++);
        m_slotGrid->addWidget(clearButton, row, col++);
        ++row;
    }

    // Updates the slot select buttons in case we're editing a connected operator
    for (s32 slotIndex = 0; slotIndex < slotCount; ++slotIndex)
    {
        if (m_op->getSlot(slotIndex)->inputPipe)
            inputSelected(slotIndex);
    }
}

QString makeSlotSourceString(Slot *slot)
{
    Q_ASSERT(slot->inputPipe);
    Q_ASSERT(slot->inputPipe->source);

    QString result = slot->inputPipe->source->objectName();

    if (slot->paramIndex != Slot::NoParamIndex)
    {
        result = QString("%1[%2]").arg(result).arg(slot->paramIndex);
    }

    return result;
}

void AddEditOperatorWidget::inputSelected(s32 slotIndex)
{
    if (slotIndex >= 0)
    {
        Slot *slot = m_op->getSlot(slotIndex);
        Q_ASSERT(slot);
        qDebug() << __PRETTY_FUNCTION__ << slot;

        auto selectButton = m_selectButtons[slotIndex];
        QSignalBlocker b(selectButton);
        selectButton->setChecked(false);
        selectButton->setText(makeSlotSourceString(slot));
    }

    m_opConfigWidget->inputSelected(slotIndex);

    bool enableOkButton = true;

    for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
    {
        if (!m_op->getSlot(slotIndex)->inputPipe)
        {
            enableOkButton = false;
            break;
        }
    }

    m_buttonBox->button(QDialogButtonBox::Ok)->setEnabled(enableOkButton);
    m_buttonBox->button(QDialogButtonBox::Ok)->setFocus();
    m_inputSelectActive = false;
}

void AddEditOperatorWidget::accept()
{
    qDebug() << __PRETTY_FUNCTION__;

    AnalysisPauser pauser(m_eventWidget->getContext());
    m_opConfigWidget->configureOperator();
    if (m_opPtr)
    {
        // add mode
        m_eventWidget->addOperator(m_opPtr, m_userLevel);
    }
    else
    {
        // edit mode
        m_eventWidget->operatorEdited(m_op);
    }
    m_eventWidget->getContext()->getAnalysis()->setModified();
    m_eventWidget->endSelectInput();
    m_eventWidget->uniqueWidgetCloses();
    QDialog::accept();
}

void AddEditOperatorWidget::reject()
{
    qDebug() << __PRETTY_FUNCTION__;

    AnalysisPauser pauser(m_eventWidget->getContext());

    if (m_opPtr)
    {
        // add mode
        // The operator will not be added to the analysis. This means any slots
        // connected by the user must be disconnected again to avoid having
        // stale connections in the source operators.
        for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
        {
            Slot *slot = m_op->getSlot(slotIndex);
            slot->disconnectPipe();
        }
    }
    else
    {
        // edit mode
        // Restore previous slot connections.

        if (m_op->hasVariableNumberOfSlots()
            && (m_op->getNumberOfSlots() != m_slotBackups.size()))
        {
            // Restore the original number of inputs.
            while (m_op->removeLastSlot());

            while (m_op->getNumberOfSlots() < m_slotBackups.size())
            {
                m_op->addSlot();
            }
        }

        Q_ASSERT(m_op->getNumberOfSlots() == m_slotBackups.size());

        bool wasModified = false;

        for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
        {
            Slot *slot = m_op->getSlot(slotIndex);
            auto oldConnection = m_slotBackups[slotIndex];
            if (slot->inputPipe != oldConnection.inputPipe
                || slot->paramIndex != oldConnection.paramIndex)
            {
                wasModified = true;
                slot->connectPipe(oldConnection.inputPipe, oldConnection.paramIndex);
            }
        }

        if (wasModified)
        {
            do_beginRun_forward(m_op);
        }
    }
    m_eventWidget->endSelectInput();
    m_eventWidget->uniqueWidgetCloses();
    QDialog::reject();
}

bool AddEditOperatorWidget::eventFilter(QObject *watched, QEvent *event)
{
    auto button = qobject_cast<QPushButton *>(watched);
    if (button && (event->type() == QEvent::Enter || event->type() == QEvent::Leave)
        && !m_inputSelectActive)
    {
        // On slot select button mouse enter/leave events highlight/unhighlight
        // the corresponding nodes in the analysis trees.
        if (auto slot = m_op->getSlot(m_selectButtons.indexOf(button)))
        {
            m_eventWidget->highlightInputOf(slot, event->type() == QEvent::Enter);
        }
    }

    // Do not filter the event out.
    return false;
}

/* Resizes the widget to have a minimum size when it's first shown.
 *
 * When instead using setMinimumSize() directly the widget can be resized
 * smaller to what its child layout would normally allow. This causes shrunken
 * input fields when for example the calibration table is shown after input is
 * selected.
 *
 * Maybe there's a better way to achieve the same but I didn't find it.
 */
void AddEditOperatorWidget::resizeEvent(QResizeEvent *event)
{
    if (!m_resizeEventSeen)
    {
        m_resizeEventSeen = true;

        auto sz = size();

        if (sz.height() < WidgetMinHeight)
            sz.setHeight(WidgetMinHeight);

        if (sz.width() < WidgetMinWidth)
            sz.setWidth(WidgetMinWidth);

        resize(sz);
    }
}

//
// OperatorConfigurationWidget
//

static const s32 SlotIndexRole  = Qt::UserRole;
static const s32 ParamIndexRole = Qt::UserRole + 1;

using ArrayMappings = QVector<ArrayMap::IndexPair>;

static void debug_dump(const ArrayMappings &mappings)
{
    for (s32 i=0; i<mappings.size(); ++i)
    {
        qDebug("num=%3d, slot=%d, param=%d",
               i, mappings[i].slotIndex, mappings[i].paramIndex);
    }
}

static QDoubleSpinBox *make_parameter_spinbox()
{
    auto result = new QDoubleSpinBox;

    result->setDecimals(8);
    result->setMinimum(-1e20);
    result->setMaximum(+1e20);
    result->setValue(result->minimum());
    result->setSpecialValueText(QSL("not set"));

    return result;
}

static bool is_set_to_min(QDoubleSpinBox *spin)
{
    return (spin->value() == spin->minimum());
}

static void repopulate_arrayMap_tables(ArrayMap *arrayMap, const ArrayMappings &mappings, QTableWidget *tw_input, QTableWidget *tw_output)
{
    Q_ASSERT(arrayMap && tw_input && tw_output);

    //qDebug() << __PRETTY_FUNCTION__;
    //debug_dump(mappings);

    tw_input->clearContents();
    tw_input->setRowCount(0);

    tw_output->clearContents();
    tw_output->setRowCount(0);

    const s32 slotCount = arrayMap->getNumberOfSlots();

    auto make_table_item = [](const QString &name, s32 slotIndex, s32 paramIndex)
    {
        auto item = new QTableWidgetItem;

        item->setData(Qt::DisplayRole, QString("%1[%2]")
                      .arg(name)
                      .arg(paramIndex));

        item->setData(SlotIndexRole, slotIndex);
        item->setData(ParamIndexRole, paramIndex);

        item->setFlags(item->flags() & ~Qt::ItemIsEditable);

        return item;
    };

    // Fill the input table
    for (s32 slotIndex = 0;
         slotIndex < slotCount;
         ++slotIndex)
    {
        auto slot = arrayMap->getSlot(slotIndex);

        if (!slot->isConnected())
            continue;

        const s32 paramCount = slot->inputPipe->parameters.size();

        for (s32 paramIndex = 0;
             paramIndex < paramCount;
             ++paramIndex)
        {
            if (mappings.contains({slotIndex, paramIndex}))
                continue;

            auto item = make_table_item(slot->inputPipe->source->objectName(), slotIndex, paramIndex);

            tw_input->setRowCount(tw_input->rowCount() + 1);
            tw_input->setItem(tw_input->rowCount() - 1, 0, item);
        }
    }

    // Fill the output table
    for (const auto &mapping: mappings)
    {
        auto slot = arrayMap->getSlot(mapping.slotIndex);

        if (!slot || !slot->isConnected())
            continue;

        auto item = make_table_item(slot->inputPipe->source->objectName(), mapping.slotIndex, mapping.paramIndex);

        tw_output->setRowCount(tw_output->rowCount() + 1);
        tw_output->setItem(tw_output->rowCount() - 1, 0, item);
    }

    tw_input->resizeRowsToContents();
    tw_output->resizeRowsToContents();
}

AbstractOpConfigWidget::AbstractOpConfigWidget(OperatorInterface *op, s32 userLevel, QWidget *parent)
    : QWidget(parent)
    , m_op(op)
    , m_userLevel(userLevel)
    , m_wasNameEdited(false)
{
}

OperatorConfigurationWidget::OperatorConfigurationWidget(OperatorInterface *op, s32 userLevel, QWidget *parent)
    : AbstractOpConfigWidget(op, userLevel, parent)
{
    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);

    auto *formLayout = new QFormLayout;
    formLayout->setContentsMargins(2, 2, 2, 2);

    widgetLayout->addLayout(formLayout);

    le_name = new QLineEdit;
    connect(le_name, &QLineEdit::textEdited, this, [this](const QString &newText) {
        // If the user clears the textedit reset NameEdited to false.
        this->setNameEdited(!newText.isEmpty());
    });
    formLayout->addRow(QSL("Name"), le_name);

    le_name->setText(op->objectName());

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        le_xAxisTitle = new QLineEdit;
        le_xAxisTitle->setText(histoSink->m_xAxisTitle);
        formLayout->addRow(QSL("X Title"), le_xAxisTitle);

        combo_xBins = make_resolution_combo(Histo1DMinBits, Histo1DMaxBits, Histo1DDefBits);
        select_by_resolution(combo_xBins, histoSink->m_bins);
        formLayout->addRow(QSL("Resolution"), combo_xBins);
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        le_xAxisTitle = new QLineEdit;
        le_xAxisTitle->setText(histoSink->m_xAxisTitle);
        formLayout->addRow(QSL("X Title"), le_xAxisTitle);

        le_yAxisTitle = new QLineEdit;
        le_yAxisTitle->setText(histoSink->m_yAxisTitle);
        formLayout->addRow(QSL("Y Title"), le_yAxisTitle);

        combo_xBins = make_resolution_combo(Histo2DMinBits, Histo2DMaxBits, Histo2DDefBits);
        combo_yBins = make_resolution_combo(Histo2DMinBits, Histo2DMaxBits, Histo2DDefBits);

        select_by_resolution(combo_xBins, histoSink->m_xBins);
        select_by_resolution(combo_yBins, histoSink->m_yBins);

        formLayout->addRow(QSL("X Resolution"), combo_xBins);
        formLayout->addRow(QSL("Y Resolution"), combo_yBins);

        limits_x = make_axis_limits_ui(QSL("X Limits"),
                                       std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
                                       histoSink->m_xLimitMin, histoSink->m_xLimitMax, histoSink->hasActiveLimits(Qt::XAxis));

        // FIXME: input validation
        //connect(limits_x.rb_limited, &QAbstractButton::toggled, this, [this] (bool) { this->validateInputs(); });

        limits_y = make_axis_limits_ui(QSL("Y Limits"),
                                       std::numeric_limits<double>::lowest(), std::numeric_limits<double>::max(),
                                       histoSink->m_yLimitMin, histoSink->m_yLimitMax, histoSink->hasActiveLimits(Qt::YAxis));

        // FIXME: input validation
        //connect(limits_y.rb_limited, &QAbstractButton::toggled, this, [this] (bool) { this->validateInputs(); });

        limits_x.outerFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);
        limits_y.outerFrame->setFrameStyle(QFrame::StyledPanel | QFrame::Sunken);

        formLayout->addRow(limits_x.outerFrame);
        formLayout->addRow(limits_y.outerFrame);
    }
    else if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        le_unit = new QLineEdit;
        le_unit->setText(calibration->getUnitLabel());

        spin_unitMin = make_calibration_spinbox();
        spin_unitMax = make_calibration_spinbox();

        formLayout->addRow(QSL("Unit Label"), le_unit);
        formLayout->addRow(QSL("Unit Min"), spin_unitMin);
        formLayout->addRow(QSL("Unit Max"), spin_unitMax);

        // Find first valid min/max pair and use it to fill the spinboxes.
        for (const auto &params: calibration->getCalibrations())
        {
            if (params.isValid())
            {
                qDebug() << __PRETTY_FUNCTION__ << calibration->objectName()
                    << "setting spinbox values:" << params.unitMin << params.unitMax;
                spin_unitMin->setValue(params.unitMin);
                spin_unitMax->setValue(params.unitMax);
                break;
            }
        }

        m_calibrationTable = new QTableWidget;
        m_calibrationTable->setMinimumSize(325, 175);

        m_pb_applyGlobalCalib = new QPushButton(QIcon(":/arrow_down.png"), QSL("Apply"));
        m_pb_applyGlobalCalib->setToolTip(QSL("Apply to all addresses"));

        connect(m_pb_applyGlobalCalib, &QPushButton::clicked, this, [this] () {
            double unitMin = spin_unitMin->value();
            double unitMax = spin_unitMax->value();

            // Write spinbox values into the table
            for (s32 row = 0;
                 row < m_calibrationTable->rowCount();
                 ++row)
            {
                auto minItem = m_calibrationTable->item(row, 1);
                minItem->setData(Qt::EditRole, unitMin);

                auto maxItem = m_calibrationTable->item(row, 2);
                maxItem->setData(Qt::EditRole, unitMax);
            }
        });

        m_applyGlobalCalibFrame = new QFrame;
        auto applyCalibLayout = new QHBoxLayout(m_applyGlobalCalibFrame);
        applyCalibLayout->setContentsMargins(0, 0, 0, 0);
        applyCalibLayout->addStretch(1);
        applyCalibLayout->addWidget(m_pb_applyGlobalCalib);
        applyCalibLayout->addStretch(1);

        formLayout->addRow(m_applyGlobalCalibFrame);
        m_applyGlobalCalibFrame->setVisible(false);

        m_calibrationTable->setVisible(false);
        m_calibrationTable->setColumnCount(3);
        m_calibrationTable->setItemDelegateForColumn(1, new CalibrationItemDelegate(m_calibrationTable));
        m_calibrationTable->setItemDelegateForColumn(2, new CalibrationItemDelegate(m_calibrationTable));
        m_calibrationTable->setHorizontalHeaderLabels({"Address", "Min", "Max"});
        m_calibrationTable->verticalHeader()->setVisible(false);

        // Populates the calibration table
        inputSelected(0);

        widgetLayout->addWidget(m_calibrationTable);
    }
    else if (auto selector = qobject_cast<IndexSelector *>(op))
    {
        spin_index = new QSpinBox;
        spin_index->setMinimum(0);
        spin_index->setMaximum(std::numeric_limits<s32>::max());
        formLayout->addRow(QSL("Selected Index"), spin_index);

        spin_index->setValue(selector->getIndex());
    }
    else if (auto previous = qobject_cast<PreviousValue *>(op))
    {
        cb_keepValid = new QCheckBox(QSL("Keep valid parameters"));
        cb_keepValid->setChecked(previous->m_keepValid);

        formLayout->addRow(cb_keepValid);

        auto label = new QLabel(QSL(
                "If <i>Keep valid parameters</i> is checked the previous valid "
                "input values will be copied to the output.<br/>"
                "<br>"
                "Otherwise the input from the previous event will be "
                "copied, including invalids."
                ));
        label->setWordWrap(true);

        formLayout->addRow(label);
    }
    else if (auto sum = qobject_cast<Sum *>(op))
    {
        cb_isMean = new QCheckBox(QSL("Calculate Mean"));
        cb_isMean->setChecked(sum->m_calculateMean);

        formLayout->addRow(cb_isMean);
    }
    else if (auto arrayMap = qobject_cast<ArrayMap *>(op))
    {
        tw_input = new QTableWidget;
        tw_input->setColumnCount(1);
        tw_input->setHorizontalHeaderLabels({"Input"});
        tw_input->horizontalHeader()->setStretchLastSection(true);
        tw_input->verticalHeader()->setVisible(false);

        tw_output = new QTableWidget;
        tw_output->setColumnCount(1);
        tw_output->setHorizontalHeaderLabels({"Output"});
        tw_output->horizontalHeader()->setStretchLastSection(true);
        tw_output->verticalHeader()->setVisible(false);

        auto pb_inputToOutput = new QPushButton(QIcon(":/arrow_right.png"), QString());
        pb_inputToOutput->setEnabled(false);

        auto pb_outputToInput = new QPushButton(QIcon(":/arrow_left.png"), QString());
        pb_outputToInput->setEnabled(false);

        connect(tw_input, &QTableWidget::itemSelectionChanged, this, [this, pb_inputToOutput] () {
            pb_inputToOutput->setEnabled(tw_input->selectedItems().size() > 0);
        });

        connect(tw_output, &QTableWidget::itemSelectionChanged, this, [this, pb_outputToInput] () {
            pb_outputToInput->setEnabled(tw_output->selectedItems().size() > 0);
        });

        connect(pb_inputToOutput, &QPushButton::clicked, this, [this, arrayMap] () {
            for (auto item: tw_input->selectedItems())
            {
                ArrayMap::IndexPair ip;
                ip.slotIndex = item->data(SlotIndexRole).toInt();
                ip.paramIndex = item->data(ParamIndexRole).toInt();
                m_arrayMappings.push_back(ip);
            }
            repopulate_arrayMap_tables(arrayMap, m_arrayMappings, tw_input, tw_output);
        });

        connect(pb_outputToInput, &QPushButton::clicked, this, [this, arrayMap] () {
            for (auto item: tw_output->selectedItems())
            {
                ArrayMap::IndexPair ip;
                ip.slotIndex = item->data(SlotIndexRole).toInt();
                ip.paramIndex = item->data(ParamIndexRole).toInt();
                m_arrayMappings.removeOne(ip);
            }
            repopulate_arrayMap_tables(arrayMap, m_arrayMappings, tw_input, tw_output);
        });

        auto buttonsLayout = new QVBoxLayout;
        buttonsLayout->setContentsMargins(0, 0, 0, 0);
        buttonsLayout->addStretch();
        buttonsLayout->addWidget(pb_inputToOutput);
        buttonsLayout->addWidget(pb_outputToInput);
        buttonsLayout->addStretch();

        auto listsLayout = new QHBoxLayout;
        listsLayout->addWidget(tw_input);
        listsLayout->addLayout(buttonsLayout);
        listsLayout->addWidget(tw_output);

        widgetLayout->addLayout(listsLayout);

        m_arrayMappings = arrayMap->m_mappings;
        repopulate_arrayMap_tables(arrayMap, m_arrayMappings, tw_input, tw_output);
    }
    else if (auto rangeFilter = qobject_cast<RangeFilter1D *>(op))
    {
        spin_minValue = new QDoubleSpinBox(this);
        spin_minValue->setDecimals(8);
        spin_minValue->setMinimum(-1e20);
        spin_minValue->setMaximum(+1e20);
        spin_minValue->setValue(spin_minValue->minimum());
        spin_minValue->setSpecialValueText(QSL("not set"));

        spin_maxValue = new QDoubleSpinBox(this);
        spin_maxValue->setDecimals(8);
        spin_maxValue->setMinimum(-1e20);
        spin_maxValue->setMaximum(+1e20);
        spin_maxValue->setValue(spin_maxValue->minimum());
        spin_maxValue->setSpecialValueText(QSL("not set"));

        rb_keepInside = new QRadioButton(QSL("Keep if inside range"), this);
        rb_keepOutside = new QRadioButton(QSL("Keep if outside range"), this);

        auto buttonGroup = new QButtonGroup(this);
        buttonGroup->addButton(rb_keepInside);
        buttonGroup->addButton(rb_keepOutside);

        auto radioLayout = new QHBoxLayout;
        radioLayout->setContentsMargins(0, 0, 0, 0);
        radioLayout->addWidget(rb_keepInside);
        radioLayout->addWidget(rb_keepOutside);

        formLayout->addRow(QSL("Min Value"), spin_minValue);
        formLayout->addRow(QSL("Max Value"), spin_maxValue);
        formLayout->addRow(radioLayout);

        if (!std::isnan(rangeFilter->m_minValue))
        {
            spin_minValue->setValue(rangeFilter->m_minValue);
        }

        if (!std::isnan(rangeFilter->m_maxValue))
        {
            spin_maxValue->setValue(rangeFilter->m_maxValue);
        }

        if (rangeFilter->m_keepOutside)
        {
            rb_keepOutside->setChecked(true);
        }
        else
        {
            rb_keepInside->setChecked(true);
        }
    }
    else if (auto filter = qobject_cast<RectFilter2D *>(op))
    {
        spin_xMin = make_parameter_spinbox();
        spin_xMax = make_parameter_spinbox();
        spin_yMin = make_parameter_spinbox();
        spin_yMax = make_parameter_spinbox();

        rb_opAnd = new QRadioButton(QSL("AND"));
        rb_opOr  = new QRadioButton(QSL("OR"));

        auto buttonGroup = new QButtonGroup(this);
        buttonGroup->addButton(rb_opAnd);
        buttonGroup->addButton(rb_opOr);

        formLayout->addRow(QSL("X Min"), spin_xMin);
        formLayout->addRow(QSL("X Max"), spin_xMax);
        formLayout->addRow(QSL("Y Min"), spin_yMin);
        formLayout->addRow(QSL("Y Max"), spin_yMax);

        auto radioLayout = new QHBoxLayout;
        radioLayout->setContentsMargins(0, 0, 0, 0);
        radioLayout->addWidget(rb_opAnd);
        radioLayout->addWidget(rb_opOr);

        formLayout->addRow(QSL("Condition"), radioLayout);

        if (filter->getXInterval().isValid())
        {
            spin_xMin->setValue(filter->getXInterval().minValue());
            spin_xMax->setValue(filter->getXInterval().maxValue());
        }

        if (filter->getYInterval().isValid())
        {
            spin_yMin->setValue(filter->getYInterval().minValue());
            spin_yMax->setValue(filter->getYInterval().maxValue());
        }

        if (filter->getConditionOp() == RectFilter2D::OpAnd)
        {
            rb_opAnd->setChecked(true);
        }
        else
        {
            rb_opOr->setChecked(true);
        }
    }
    else if (auto binOp = qobject_cast<BinarySumDiff *>(op))
    {
        combo_equation = new QComboBox;
        combo_equation->setFont(make_monospace_font());

        for (s32 idx = 0; idx < binOp->getNumberOfEquations(); ++idx)
        {
            combo_equation->addItem(binOp->getEquationDisplayString(idx), idx);
        }

        combo_equation->setCurrentIndex(binOp->getEquation());

        le_unit = new QLineEdit;
        le_unit->setText(binOp->getOutputUnitLabel());

        spin_outputLowerLimit = new QDoubleSpinBox;
        spin_outputUpperLimit = new QDoubleSpinBox;

        for (auto spin: { spin_outputLowerLimit, spin_outputUpperLimit })
        {
            spin->setDecimals(8);
            spin->setMinimum(-1e20);
            spin->setMaximum(+1e20);
        }

        spin_outputLowerLimit->setValue(binOp->getOutputLowerLimit());
        spin_outputUpperLimit->setValue(binOp->getOutputUpperLimit());

        pb_autoLimits = new QPushButton(QSL("Auto Limits"));
        connect(pb_autoLimits, &QPushButton::clicked, this,
                [this, binOp]() { this->updateOutputLimits(binOp); });

        formLayout->addRow(QSL("Equation"), combo_equation);
        formLayout->addRow(QSL("Output Unit"), le_unit);
        formLayout->addRow(QSL("Output Lower Limit"), spin_outputLowerLimit);
        formLayout->addRow(QSL("Output Upper Limit"), spin_outputUpperLimit);
        formLayout->addRow(QSL(""), pb_autoLimits);
    }
    else if (auto aggOp = qobject_cast<AggregateOps *>(op))
    {
        // operation select combo
        combo_aggOp = new QComboBox();
        for (s32 i = 0; i < AggregateOps::NumOps; ++i)
        {
            combo_aggOp->addItem(AggregateOps::getOperationName(
                    static_cast<AggregateOps::Operation>(i)), i);
        }
        combo_aggOp->setCurrentIndex(static_cast<s32>(aggOp->getOperation()));

        // unit label
        le_unit = new QLineEdit;
        le_unit->setText(aggOp->getOutputUnitLabel());

        // thresholds
        cb_useMinThreshold = new QCheckBox;
        cb_useMaxThreshold = new QCheckBox;
        spin_minThreshold = new QDoubleSpinBox;
        spin_maxThreshold = new QDoubleSpinBox;

        connect(cb_useMinThreshold, &QCheckBox::stateChanged,
                this, [this] (int cbState) {
                    spin_minThreshold->setEnabled(cbState == Qt::Checked);
                });

        connect(cb_useMaxThreshold, &QCheckBox::stateChanged,
                this, [this] (int cbState) {
                    spin_maxThreshold->setEnabled(cbState == Qt::Checked);
                });

        for (auto spin: { spin_minThreshold, spin_maxThreshold })
        {
            spin->setDecimals(8);
            spin->setMinimum(-1e20);
            spin->setMaximum(+1e20);
            spin->setValue(0.0);
            spin->setEnabled(false);
        }

        double minT = aggOp->getMinThreshold();
        double maxT = aggOp->getMaxThreshold();

        if (!std::isnan(minT))
        {
            spin_minThreshold->setValue(minT);
            cb_useMinThreshold->setChecked(true);
        }
        else
        {
            cb_useMinThreshold->setChecked(false);
        }

        if (!std::isnan(maxT))
        {
            spin_maxThreshold->setValue(maxT);
            cb_useMaxThreshold->setChecked(true);
        }
        else
        {
            cb_useMaxThreshold->setChecked(false);
        }

        auto minTLayout = new QHBoxLayout;
        minTLayout->setContentsMargins(0, 0, 0, 0);
        minTLayout->setSpacing(2);

        minTLayout->addWidget(cb_useMinThreshold);
        minTLayout->addWidget(spin_minThreshold);

        auto maxTLayout = new QHBoxLayout;
        maxTLayout->setContentsMargins(0, 0, 0, 0);
        maxTLayout->setSpacing(2);

        maxTLayout->addWidget(cb_useMaxThreshold);
        maxTLayout->addWidget(spin_maxThreshold);

        formLayout->addRow(QSL("Operation"), combo_aggOp);
        formLayout->addRow(QSL("Threshold Min"), minTLayout);
        formLayout->addRow(QSL("Threshold Max"), maxTLayout);
        formLayout->addRow(QSL("Output Unit"), le_unit);
        formLayout->addRow(new QLabel(QSL("Leave output unit blank to copy from input.")));
    }
}

// NOTE: This will be called after construction for each slot by AddEditOperatorWidget::repopulateSlotGrid()!
void OperatorConfigurationWidget::inputSelected(s32 slotIndex)
{
    OperatorInterface *op = m_op;

    if (no_input_connected(op) && !wasNameEdited())
    {
        le_name->clear();
        setNameEdited(false);
    }

    //
    // Operator name
    //
    if (!wasNameEdited())
    {
        // The name field is empty or was never modified by the user. Update its
        // contents to reflect the newly selected input(s).

        if (op->getNumberOfSlots() == 1 && op->getSlot(0)->isConnected())
        {
            le_name->setText(makeSlotSourceString(op->getSlot(0)));
        }
        else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
        {
            if (histoSink->m_inputX.isConnected() && histoSink->m_inputY.isConnected())
            {
                QString nameX = makeSlotSourceString(&histoSink->m_inputX);
                QString nameY = makeSlotSourceString(&histoSink->m_inputY);
                le_name->setText(QString("%1_vs_%2").arg(nameX).arg(nameY));
            }
        }
        else if (auto difference = qobject_cast<Difference *>(op))
        {
            if (difference->m_inputA.isConnected() && difference->m_inputB.isConnected())
            {
                QString nameA = makeSlotSourceString(&difference->m_inputA);
                QString nameB = makeSlotSourceString(&difference->m_inputB);
                le_name->setText(QString("%1 - %2").arg(nameA).arg(nameB));
            }
        }
        else if (auto condFilter = qobject_cast<ConditionFilter *> (op))
        {
            if (condFilter->m_dataInput.isConnected() && condFilter->m_conditionInput.isConnected())
            {
                QString dataName = makeSlotSourceString(&condFilter->m_dataInput);
                QString condName = makeSlotSourceString(&condFilter->m_conditionInput);
                le_name->setText(QString("%1_if_%2").arg(dataName).arg(condName));
            }
        }
        else if (auto filter = qobject_cast<RectFilter2D *>(op))
        {
            if (filter->getSlot(0)->isConnected() && filter->getSlot(1)->isConnected())
            {
                QString nameX = makeSlotSourceString(filter->getSlot(0));
                QString nameY = makeSlotSourceString(filter->getSlot(1));
                le_name->setText(QString("%1_%2").arg(nameX).arg(nameY)); // FIXME: better name here
            }
        }
    }
    else
    {
        le_name->setText(op->objectName());
    }

    if (!le_name->text().isEmpty() && op->getNumberOfOutputs() > 0 && all_inputs_connected(op) && !wasNameEdited())
    {
        // XXX: leftoff here TODO: use the currently selected operations name
        // as the suffix (currently it always says 'sum')
#if 0
        if (auto aggOp = qobject_cast<AggregateOps *>(op))
        {

        }
        else
#endif
        {
            // Append the lowercase short name for non sinks
            auto suffix = QSL(".") + op->getShortName().toLower();
            auto name = le_name->text();
            if (!name.endsWith(suffix))
            {
                name += suffix;
                le_name->setText(name);
            }
        }
    }

    //
    // Operator specific actions
    //

    Slot *slot = (slotIndex >= 0 ? op->getSlot(slotIndex) : nullptr);

    if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        Q_ASSERT(slot);

        if (slot->isConnected())
        {
            // If connected to array:
            //   use first calibration values for spinbox
            //   if those are not valid use first input param values for spinbox
            // if connected to param:
            //   use calibration for param for spinbox
            //   if that is not valid use input param values for spinbox
            // if nothing is valid keep spinbox values

            double proposedMin = spin_unitMin->value();
            double proposedMax = spin_unitMax->value();

            s32 paramIndex = (slot->paramIndex == Slot::NoParamIndex ? 0 : slot->paramIndex);
            auto params = calibration->getCalibration(paramIndex);

            if (params.isValid())
            {
                proposedMin = params.unitMin;
                proposedMax = params.unitMax;
            }
            else if (auto inParam = slot->inputPipe->getParameter(paramIndex))
            {
                proposedMin = inParam->lowerLimit;
                proposedMax = inParam->upperLimit;
            }

            spin_unitMin->setValue(proposedMin);
            spin_unitMax->setValue(proposedMax);

            if (slot->paramIndex == Slot::NoParamIndex)
            {
                fillCalibrationTable(calibration, proposedMin, proposedMax);
                m_calibrationTable->setVisible(true);
                m_applyGlobalCalibFrame->setVisible(true);
            }
            else
            {
                m_calibrationTable->setVisible(false);
                m_applyGlobalCalibFrame->setVisible(false);
            }
        }
        else
        {
            // Not connected -> Make spinboxes show "Not Set" and hide table and button
            spin_unitMin->setValue(spin_unitMin->minimum());
            spin_unitMax->setValue(spin_unitMax->minimum());
            m_calibrationTable->setVisible(false);
            m_applyGlobalCalibFrame->setVisible(false);
        }
    }
    else if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        if (le_xAxisTitle->text().isEmpty())
        {
            le_xAxisTitle->setText(le_name->text());
        }
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        if (le_xAxisTitle->text().isEmpty() && histoSink->m_inputX.isConnected())
            le_xAxisTitle->setText(makeSlotSourceString(&histoSink->m_inputX));

        if (le_yAxisTitle->text().isEmpty() && histoSink->m_inputY.isConnected())
            le_yAxisTitle->setText(makeSlotSourceString(&histoSink->m_inputY));

        // x input was selected
        if (histoSink->m_inputX.isConnected() && slot == &histoSink->m_inputX && std::isnan(histoSink->m_xLimitMin))
        {
            limits_x.spin_min->setValue(slot->inputPipe->parameters[slot->paramIndex].lowerLimit);
            limits_x.spin_max->setValue(slot->inputPipe->parameters[slot->paramIndex].upperLimit);
        }

        // y input was selected
        if (histoSink->m_inputY.isConnected() && slot == &histoSink->m_inputY && std::isnan(histoSink->m_yLimitMin))
        {
            limits_y.spin_min->setValue(slot->inputPipe->parameters[slot->paramIndex].lowerLimit);
            limits_y.spin_max->setValue(slot->inputPipe->parameters[slot->paramIndex].upperLimit);
        }
    }
    else if (auto arrayMap = qobject_cast<ArrayMap *>(op))
    {
        repopulate_arrayMap_tables(arrayMap, m_arrayMappings, tw_input, tw_output);
    }
    else if (auto rangeFilter = qobject_cast<RangeFilter1D *>(op))
    {
        Q_ASSERT(slot);

        if (slot->isConnected())
        {
            // use the first parameters min and max values to fill the spinboxes
            s32 paramIndex = (slot->paramIndex == Slot::NoParamIndex ? 0 : slot->paramIndex);
            Parameter *param = slot->inputPipe->getParameter(paramIndex);

            if (spin_minValue->value() == spin_minValue->minimum())
            {
                if (param)
                {
                    spin_minValue->setValue(param->lowerLimit);
                }
                else
                {
                    spin_minValue->setValue(0.0);
                }
            }

            if (spin_maxValue->value() == spin_maxValue->minimum())
            {
                if (param)
                {
                    spin_maxValue->setValue(param->upperLimit);
                }
                else
                {
                    spin_maxValue->setValue(0.0);
                }
            }
        }
    }
    else if (auto filter = qobject_cast<RectFilter2D *>(op))
    {
        Q_ASSERT(slot);

        if (slot == op->getSlot(0)) // x
        {
            Q_ASSERT(slot->isParamIndexInRange());

            if (spin_xMin->value() == spin_xMin->minimum())
            {
                spin_xMin->setValue(slot->inputPipe->getParameter(slot->paramIndex)->lowerLimit);
            }

            if (spin_xMax->value() == spin_xMax->minimum())
            {
                spin_xMax->setValue(slot->inputPipe->getParameter(slot->paramIndex)->upperLimit);
            }
        }
        else if (slot == op->getSlot(1)) // y
        {
            Q_ASSERT(slot->isParamIndexInRange());

            if (spin_yMin->value() == spin_yMin->minimum())
            {
                spin_yMin->setValue(slot->inputPipe->getParameter(slot->paramIndex)->lowerLimit);
            }

            if (spin_yMax->value() == spin_yMax->minimum())
            {
                spin_yMax->setValue(slot->inputPipe->getParameter(slot->paramIndex)->upperLimit);
            }
        }
    }
}

void OperatorConfigurationWidget::configureOperator()
{
    OperatorInterface *op = m_op;

    for (s32 slotIndex = 0; slotIndex < m_op->getNumberOfSlots(); ++slotIndex)
    {
        Q_ASSERT(op->getSlot(slotIndex)->isConnected());
    }

    op->setObjectName(le_name->text());

    if (auto histoSink = qobject_cast<Histo1DSink *>(op))
    {
        histoSink->m_xAxisTitle = le_xAxisTitle->text();

        s32 bins = combo_xBins->currentData().toInt();
        histoSink->m_bins = bins;
        // Actually updating the histograms is done in Histo1DSink::beginRun();
    }
    else if (auto histoSink = qobject_cast<Histo2DSink *>(op))
    {
        histoSink->m_xAxisTitle = le_xAxisTitle->text();
        histoSink->m_yAxisTitle = le_yAxisTitle->text();

        s32 xBins = combo_xBins->currentData().toInt();
        s32 yBins = combo_yBins->currentData().toInt();

        histoSink->m_xBins = xBins;
        histoSink->m_yBins = yBins;

        if (limits_x.rb_limited->isChecked())
        {
            histoSink->m_xLimitMin = limits_x.spin_min->value();
            histoSink->m_xLimitMax = limits_x.spin_max->value();
        }
        else
        {
            histoSink->m_xLimitMin = make_quiet_nan();
            histoSink->m_xLimitMax = make_quiet_nan();
        }

        if (limits_y.rb_limited->isChecked())
        {
            histoSink->m_yLimitMin = limits_y.spin_min->value();
            histoSink->m_yLimitMax = limits_y.spin_max->value();
        }
        else
        {
            histoSink->m_yLimitMin = make_quiet_nan();
            histoSink->m_yLimitMax = make_quiet_nan();
        }

        // Same as for Histo1DSink: the histogram is created or updated in Histo2DSink::beginRun()
    }
    else if (auto calibration = qobject_cast<CalibrationMinMax *>(op))
    {
        calibration->setUnitLabel(le_unit->text());

        double unitMin = spin_unitMin->value();
        double unitMax = spin_unitMax->value();

        if (unitMin == spin_unitMin->minimum())
            unitMin = make_quiet_nan();

        if (unitMax == spin_unitMax->minimum())
            unitMax = make_quiet_nan();


        for (s32 addr = 0; addr < op->getSlot(0)->inputPipe->parameters.size(); ++addr)
        {
            if (op->getSlot(0)->paramIndex != Slot::NoParamIndex)
            {
                // Connected to specific address -> use spinbox values for that address
                if (op->getSlot(0)->paramIndex == addr)
                {
                    calibration->setCalibration(addr, unitMin, unitMax);
                }
                else
                {
                    // Set invalid params for other addresses
                    calibration->setCalibration(addr, CalibrationMinMaxParameters());
                }
            }
            else
            {
                // Connected to an array -> use table values
                unitMin = unitMax = make_quiet_nan();

                if (auto item = m_calibrationTable->item(addr, 1))
                    unitMin = item->data(Qt::EditRole).toDouble();

                if (auto item = m_calibrationTable->item(addr, 2))
                    unitMax = item->data(Qt::EditRole).toDouble();

                calibration->setCalibration(addr, unitMin, unitMax);
            }
        }
    }
    else if (auto selector = qobject_cast<IndexSelector *>(op))
    {
        s32 index = spin_index->value();
        selector->setIndex(index);
    }
    else if (auto previous = qobject_cast<PreviousValue *>(op))
    {
        previous->m_keepValid = cb_keepValid->isChecked();
    }
    else if (auto sum = qobject_cast<Sum *>(op))
    {
        sum->m_calculateMean = cb_isMean->isChecked();
    }
    else if (auto arrayMap = qobject_cast<ArrayMap *>(op))
    {
        /* Remove mappings that got invalidated due to removing slots from the
         * ArrayMap, then copy the mappings over. */
        s32 nSlots = arrayMap->getNumberOfSlots();

        qDebug() << __PRETTY_FUNCTION__ << "local mappings before erase" << m_arrayMappings.size();

        m_arrayMappings.erase(
            std::remove_if(m_arrayMappings.begin(), m_arrayMappings.end(), [nSlots](const ArrayMap::IndexPair &ip) {
                bool result = ip.slotIndex >= nSlots;
                qDebug() << __PRETTY_FUNCTION__ << "nSlots =" << nSlots << "ip.slotIndex =" << ip.slotIndex << "result =" << result;
                return result;
            }),
            m_arrayMappings.end());

        qDebug() << __PRETTY_FUNCTION__ << "local mappings after erase" << m_arrayMappings.size();

        arrayMap->m_mappings = m_arrayMappings;
    }
    else if (auto rangeFilter = qobject_cast<RangeFilter1D *>(op))
    {
        rangeFilter->m_minValue = spin_minValue->value();
        rangeFilter->m_maxValue = spin_maxValue->value();
        rangeFilter->m_keepOutside = rb_keepOutside->isChecked();
    }
    else if (auto filter = qobject_cast<RectFilter2D *>(op))
    {
        if (is_set_to_min(spin_xMin) || is_set_to_min(spin_xMax))
        {
            filter->setXInterval(QwtInterval()); // set an invalid interval
        }
        else
        {
            filter->setXInterval(spin_xMin->value(), spin_xMax->value());
        }

        if (is_set_to_min(spin_yMin) || is_set_to_min(spin_yMax))
        {
            filter->setYInterval(QwtInterval()); // set an invalid interval
        }
        else
        {
            filter->setYInterval(spin_yMin->value(), spin_yMax->value());
        }

        filter->setConditionOp(rb_opAnd->isChecked() ? RectFilter2D::OpAnd : RectFilter2D::OpOr);
    }
    else if (auto binOp = qobject_cast<BinarySumDiff *>(op))
    {
        binOp->setEquation(combo_equation->currentData().toInt());
        binOp->setOutputUnitLabel(le_unit->text());
        binOp->setOutputLowerLimit(spin_outputLowerLimit->value());
        binOp->setOutputUpperLimit(spin_outputUpperLimit->value());
    }
    else if (auto aggOp = qobject_cast<AggregateOps *>(op))
    {
        aggOp->setOperation(static_cast<AggregateOps::Operation>(combo_aggOp->currentData().toInt()));

        double minT = spin_minThreshold->value();
        double maxT = spin_maxThreshold->value();

        aggOp->setMinThreshold(cb_useMinThreshold->isChecked() ? minT : make_quiet_nan());
        aggOp->setMaxThreshold(cb_useMaxThreshold->isChecked() ? maxT : make_quiet_nan());

        aggOp->setOutputUnitLabel(le_unit->text());
    }
}

void OperatorConfigurationWidget::fillCalibrationTable(CalibrationMinMax *calib, double proposedMin, double proposedMax)
{
    Q_ASSERT(calib->getSlot(0)->isConnected());

    s32 paramCount = calib->getSlot(0)->inputPipe->parameters.size();

    m_calibrationTable->setRowCount(paramCount);

    for (s32 addr = 0; addr < paramCount; ++addr)
    {
        // address
        auto item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsEnabled);
        item->setData(Qt::DisplayRole, addr);
        m_calibrationTable->setItem(addr, 0, item);

        auto calibParams = calib->getCalibration(addr);
        double unitMin = calibParams.isValid() ? calibParams.unitMin : proposedMin;
        double unitMax = calibParams.isValid() ? calibParams.unitMax : proposedMax;

        // min
        item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        item->setData(Qt::EditRole, unitMin);
        m_calibrationTable->setItem(addr, 1, item);

        // max
        item = new QTableWidgetItem;
        item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled | Qt::ItemIsEditable);
        item->setData(Qt::EditRole, unitMax);
        m_calibrationTable->setItem(addr, 2, item);
    }

    m_calibrationTable->resizeRowsToContents();
}

void OperatorConfigurationWidget::updateOutputLimits(BinarySumDiff *op)
{
    if (!all_inputs_connected(op))
        return;

    int equationIndex = combo_equation->currentData().toInt();
    double llA = op->getSlot(0)->inputPipe->parameters.value(0).lowerLimit;
    double ulA = op->getSlot(0)->inputPipe->parameters.value(0).upperLimit;

    double llB = op->getSlot(1)->inputPipe->parameters.value(0).lowerLimit;
    double ulB = op->getSlot(1)->inputPipe->parameters.value(0).upperLimit;

    double llO = 0.0;
    double ulO = 0.0;

    switch (equationIndex)
    {
        case 0: // C = A + B
            {
                llO = llA + llB;
                ulO = ulA + ulB;
            } break;

        case 1: // C = A - B
            {
                llO = llA - ulB;
                ulO = ulA - llB;
            } break;

        case 2: // C = (A + B) / (A - B)
            {
                llO = (ulA + llB) / (ulA - llB);
                ulO = (llA + ulB) / (llA - ulB);
            } break;

        case 3: // C = (A - B) / (A + B)
            {
                llO = (ulA - ulB) / (ulA + ulB);
                ulO = (llA - llB) / (llA + llB);
            } break;

        case 4: // C = A / (A - B)
            {
                llO = ulA / (ulA - llB);
                ulO = llA / (llA - ulB);
            } break;

        case 5: // C = (A - B) / A
            {
                llO = (ulA - llB) / ulA;
                ulO = (llA - ulB) / llA;
            } break;
    }

    spin_outputLowerLimit->setValue(llO);
    spin_outputUpperLimit->setValue(ulO);
}

struct RateTypeInfo
{
    RateMonitorSink::Type type;
    QString name;
    QString description;
};

static const std::array<RateTypeInfo, 3> RateTypeInfos =
{
    {
        {
            RateMonitorSink::Type::CounterDifference,
            QSL("Counter Difference"),
            QSL("Input values are interpreted as increasing counter values.\n"
                "The resulting rate is calculated from the difference of successive input values.")
        },

        {
            RateMonitorSink::Type::PrecalculatedRate,
            QSL("Precalculated Rate"),
            QSL("Input values are interpreted as rate values and are directly recorded.")
        },

        {
            RateMonitorSink::Type::FlowRate,
            QSL("Flow Rate"),
            QSL("The rate of flow through the input array is calculated and recorded.")
        },
    }
};

//
// RateMonitorConfigWidget
//
RateMonitorConfigWidget::RateMonitorConfigWidget(RateMonitorSink *rms, s32 userLevel, QWidget *parent)
    : AbstractOpConfigWidget(rms, userLevel, parent)
    , m_rms(rms)
{
    auto widgetLayout = new QVBoxLayout(this);
    widgetLayout->setContentsMargins(0, 0, 0, 0);

    auto *formLayout = new QFormLayout;
    formLayout->setContentsMargins(2, 2, 2, 2);

    widgetLayout->addLayout(formLayout);

    le_name = new QLineEdit;
    connect(le_name, &QLineEdit::textEdited, this, [this](const QString &newText) {
        // If the user clears the textedit reset NameEdited to false.
        this->setNameEdited(!newText.isEmpty());
    });

    le_name->setText(rms->objectName());

    // rate type selection and explanation of what each type does
    combo_type = new QComboBox;
    auto stack_descriptions = new QStackedWidget;

    for (const auto &info: RateTypeInfos)
    {
        combo_type->addItem(info.name, static_cast<s32>(info.type));

        auto label_description = new QLabel(info.description);
        label_description->setWordWrap(true);
        label_description->setAlignment(Qt::AlignLeft | Qt::AlignTop);

        stack_descriptions->addWidget(label_description);
    }

    connect(combo_type, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
            stack_descriptions, &QStackedWidget::setCurrentIndex);

    combo_type->setCurrentIndex(combo_type->findData(static_cast<s32>(m_rms->getType())));

    // rate history capacity
    spin_capacity = new QSpinBox;
    spin_capacity->setMinimum(1);
    spin_capacity->setMaximum(1u << 20);
    spin_capacity->setValue(m_rms->getRateHistoryCapacity());

    // unit label and calibration
    le_unit = new QLineEdit;
    le_unit->setText(m_rms->getUnitLabel());
    spin_factor = make_calibration_spinbox(QString());
    spin_factor->setValue(m_rms->getCalibrationFactor());
    spin_offset = make_calibration_spinbox(QString());
    spin_offset->setValue(m_rms->getCalibrationOffset());

    // populate the layout
    formLayout->addRow(QSL("Name"), le_name);
    formLayout->addRow(QSL("Type"), combo_type);
    formLayout->addRow(QSL("Description"), stack_descriptions);
    formLayout->addRow(QSL("Max samples"), spin_capacity);
    formLayout->addRow(QSL("Unit Label"), le_unit);
    formLayout->addRow(QSL("Calibration Factor"), spin_factor);
    formLayout->addRow(QSL("Calibration Offset"), spin_offset);
}

void RateMonitorConfigWidget::configureOperator()
{
    assert(all_inputs_connected(m_op));

    m_rms->setObjectName(le_name->text());
    m_rms->setType(static_cast<RateMonitorSink::Type>(combo_type->currentData().toInt()));
    m_rms->setRateHistoryCapacity(spin_capacity->value());
    m_rms->setUnitLabel(le_unit->text());
    m_rms->setCalibrationFactor(spin_factor->value());
    m_rms->setCalibrationOffset(spin_offset->value());
}

void RateMonitorConfigWidget::inputSelected(s32 slotIndex)
{
    OperatorInterface *op = m_op;

    if (no_input_connected(op) && !wasNameEdited())
    {
        le_name->clear();
        setNameEdited(false);
    }

    if (!wasNameEdited())
    {
        auto name = makeSlotSourceString(m_rms->getSlot(0));

        switch (static_cast<RateMonitorSink::Type>(combo_type->currentData().toInt()))
        {
            case RateMonitorSink::Type::CounterDifference:
            case RateMonitorSink::Type::PrecalculatedRate:
                name += QSL(".rate");
                break;

            case RateMonitorSink::Type::FlowRate:
                name += QSL(".flowRate");
                break;
        }

        le_name->setText(name);
    }

    if (all_inputs_connected(op))
    {
        // Use the inputs unit label
        le_unit->setText(op->getSlot(0)->inputPipe->parameters.unit);
    }
}

//
// PipeDisplay
//

PipeDisplay::PipeDisplay(Analysis *analysis, Pipe *pipe, QWidget *parent)
    : QWidget(parent, Qt::Tool)
    , m_analysis(analysis)
    , m_pipe(pipe)
    , m_infoLabel(new QLabel)
    , m_parameterTable(new QTableWidget)
{
    auto layout = new QGridLayout(this);
    s32 row = 0;
    s32 nCols = 1;

    auto closeButton = new QPushButton(QSL("Close"));
    connect(closeButton, &QPushButton::clicked, this, &QWidget::close);

    layout->addWidget(m_infoLabel, row++, 0);
    layout->addWidget(m_parameterTable, row++, 0);
    layout->addWidget(closeButton, row++, 0, 1, 1);

    layout->setRowStretch(1, 1);

    // columns:
    // Valid, Value, lower Limit, upper Limit
    m_parameterTable->setColumnCount(4);
    m_parameterTable->setHorizontalHeaderLabels({"Valid", "Value", "Lower Limit", "Upper Limit"});

    refresh();
}

void PipeDisplay::refresh()
{
    setWindowTitle(m_pipe->parameters.name);

    if (auto a2State = m_analysis->getA2AdapterState())
    {
        a2::PipeVectors pipe = find_output_pipe(a2State, m_pipe);

        m_parameterTable->setRowCount(pipe.data.size);

        for (s32 pi = 0; pi < pipe.data.size; pi++)
        {
            double param = pipe.data[pi];
            double lowerLimit = pipe.lowerLimits[pi];
            double upperLimit = pipe.upperLimits[pi];

            QStringList columns =
            {
                a2::is_param_valid(param) ? QSL("Y") : QSL("N"),
                a2::is_param_valid(param) ? QString::number(param) : QSL(""),
                QString::number(lowerLimit),
                QString::number(upperLimit),
            };

            for (s32 ci = 0; ci < columns.size(); ci++)
            {
                auto item = m_parameterTable->item(pi, ci);
                if (!item)
                {
                    item = new QTableWidgetItem;
                    m_parameterTable->setItem(pi, ci, item);
                }

                item->setText(columns[ci]);
                item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            }

            if (!m_parameterTable->verticalHeaderItem(pi))
            {
                m_parameterTable->setVerticalHeaderItem(pi, new QTableWidgetItem);
            }

            m_parameterTable->verticalHeaderItem(pi)->setText(QString::number(pi));
        }

        m_infoLabel->setText("a2::PipeVectors");
    }
    else
    {
        m_parameterTable->setRowCount(m_pipe->parameters.size());

        for (s32 pi = 0; pi < m_pipe->parameters.size(); ++pi)
        {
            const auto &param(m_pipe->parameters[pi]);

            QStringList columns =
            {
                param.valid ? QSL("Y") : QSL("N"),
                param.valid ? QString::number(param.value) : QSL(""),
                QString::number(param.lowerLimit),
                QString::number(param.upperLimit),
            };

            for (s32 ci = 0; ci < columns.size(); ci++)
            {
                auto item = m_parameterTable->item(pi, ci);
                if (!item)
                {
                    item = new QTableWidgetItem;
                    m_parameterTable->setItem(pi, ci, item);
                }

                item->setText(columns[ci]);
                item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
            }

            if (!m_parameterTable->verticalHeaderItem(pi))
            {
                m_parameterTable->setVerticalHeaderItem(pi, new QTableWidgetItem);
            }

            m_parameterTable->verticalHeaderItem(pi)->setText(QString::number(pi));
        }

        m_infoLabel->setText("analysis::Pipe");
    }

    m_parameterTable->resizeColumnsToContents();
    m_parameterTable->resizeRowsToContents();
}


QWidget* CalibrationItemDelegate::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
    auto result = QStyledItemDelegate::createEditor(parent, option, index);

    if (auto spin = qobject_cast<QDoubleSpinBox *>(result))
    {
        spin->setDecimals(8);
        spin->setMinimum(-1e20);
        spin->setMaximum(+1e20);
        spin->setSpecialValueText(QSL("no set"));
        spin->setValue(spin->minimum());
    }

    return result;
}

SessionErrorDialog::SessionErrorDialog(const QString &message, const QString &title, QWidget *parent)
    : QDialog(parent)
{
    if (!title.isEmpty())
    {
        setWindowTitle(title);
    }
    auto tb = new QTextBrowser(this);
    tb->setText(message);
    tb->setReadOnly(true);

    auto bb = new QDialogButtonBox(QDialogButtonBox::Close, this);

    auto bbLayout = new QHBoxLayout;
    bbLayout->addStretch(1);
    bbLayout->addWidget(bb);
    bbLayout->addStretch(1);

    auto mainLayout = new QVBoxLayout(this);
    mainLayout->addWidget(tb);
    mainLayout->addLayout(bbLayout);

    QObject::connect(bb, &QDialogButtonBox::accepted, this, &QDialog::accept);
    QObject::connect(bb, &QDialogButtonBox::rejected, this, &QDialog::reject);

    resize(600, 300);
    bb->button(QDialogButtonBox::Close)->setFocus();
}

}
