/* mvme - Mesytec VME Data Acquisition
 *
 * Copyright (C) 2016-2020 mesytec GmbH & Co. KG <info@mesytec.com>
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
#include "listfilter_extractor_dialog.h"

#include "analysis/analysis_ui_p.h"
#include "data_filter_edit.h"
#include "../mvme_context_lib.h" // AnalysisPauser

#include <QCheckBox>
#include <QComboBox>
#include <QDialogButtonBox>
#include <QGroupBox>
#include <QListWidget>
#include <QPushButton>
#include <QSignalMapper>
#include <QSpinBox>
#include <QStackedWidget>

namespace analysis
{

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
 * Uses cases:
 * - Edit an existing filter.
 * - Add a new filter. The module may already contain listfilters or this may
 *   be the very first listfilter added to the module.
 *
 * -> If there are no filters to display always create a new unnamed default
 *  filter.  On hitting apply add that filter to the analysis and repopulate
 *  the widget -> now the filter contents will be reloaded from the analysis
 *  and everything is in sync again.
 *  When deleting the last filter either close the dialog or go back to
 *  creating an unnamed default filter.
 *
 * What are we editing? The whole list of ListFilterExtractor for a certain
 * module as they all interact.
 *
 * If there are no filters we start out with a default filter for the module.
 */

/* Inputs for for editing a single ListFilter. */
struct ListFilterEditor
{
    QWidget *widget;

    QLabel *label_addressBits,
           *label_dataBits,
           *label_outputSize,
           *label_combinedBits;

    QSpinBox *spin_repetitions,
             *spin_wordCount;

    QPushButton *pb_editNameList;

    QLineEdit *le_name;

    DataFilterEdit *filter_lowWord,
                   *filter_highWord;

    QComboBox *combo_wordSize;

    QCheckBox *cb_swapWords,
              *cb_addRandom;
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

    a2::DataSourceOptions::opt_t options = 0;

    if (!e.cb_addRandom->isChecked())
        options |= a2::DataSourceOptions::NoAddedRandom;

    a2::ListFilterExtractor ex_a2 = a2::make_listfilter_extractor(
        listFilter,
        e.spin_repetitions->value(),
        0,
        options);

    return ex_a2;
}

static void listfilter_editor_update_info_labels(ListFilterEditor e)
{
    auto ex = listfilter_editor_make_a2_extractor(e);

    size_t addressBits              = get_address_bits(&ex);
    size_t addressCount             = get_address_count(&ex);
    size_t baseAddressBits          = get_base_address_bits(&ex);
    size_t repetitionAddressBits    = get_repetition_address_bits(&ex);
    size_t dataBits                 = get_extract_bits(&ex.listFilter, a2::data_filter::MultiWordFilter::CacheD);
    size_t combinedBits             = ((ex.listFilter.flags & a2::data_filter::ListFilter::WordSize32) ? 32 : 16) * ex.listFilter.wordCount;

    assert(combinedBits <= 64);

    e.label_addressBits->setText(QString("%1 (%2 base + %3 repetition)")
                                 .arg(addressBits)
                                 .arg(baseAddressBits)
                                 .arg(repetitionAddressBits));

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
    e.pb_editNameList = new QPushButton(QIcon(QSL(":/pencil.png")), QSL("Edit Name List"));
    e.spin_wordCount = new QSpinBox;
    e.filter_lowWord = makeFilterEdit();
    e.filter_highWord = makeFilterEdit();
    e.combo_wordSize = new QComboBox;
    e.cb_swapWords = new QCheckBox;
    e.cb_addRandom = new QCheckBox;

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

    // React to changes to the number of input data words consumed which
    // dictate how many bits are available after the combine step.
    auto on_nof_input_bits_changed = [e]()
    {
        auto flags          = static_cast<ListFilter::Flag>(e.combo_wordSize->currentData().toInt());
        auto wordCount      = e.spin_wordCount->value();
        size_t combinedBits = ((flags & a2::data_filter::ListFilter::WordSize32) ? 32 : 16) * wordCount;

        assert(combinedBits <= 64);

        ssize_t loBits = std::min(static_cast<ssize_t>(32), static_cast<ssize_t>(combinedBits));
        ssize_t hiBits = std::max(static_cast<ssize_t>(0),  static_cast<ssize_t>(combinedBits) - 32);

        e.filter_lowWord->setBitCount(loBits);
        e.filter_highWord->setBitCount(hiBits);
    };

    QObject::connect(e.combo_wordSize, static_cast<void (QComboBox::*) (int)>(&QComboBox::currentIndexChanged),
                     e.widget, on_nof_input_bits_changed);

    QObject::connect(e.spin_wordCount, static_cast<void (QSpinBox::*) (int)>(&QSpinBox::valueChanged),
                     e.widget, on_nof_input_bits_changed);

    on_nof_input_bits_changed();

    auto update_editor = [e]() { listfilter_editor_update_info_labels(e); };

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


    // nameList editing

    // layout
    auto layout = new QFormLayout(e.widget);

    layout->addRow("Name", e.le_name);
    layout->addRow("Repetitions", e.spin_repetitions);
    layout->addRow(QSL("Parameter Names"), e.pb_editNameList);

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

        layout_extraction->addRow("Second word", e.filter_highWord);
        layout_extraction->addRow("First word", e.filter_lowWord);
        layout_extraction->addRow("Add Random in [0, 1)", e.cb_addRandom);

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
    e.cb_addRandom->setChecked(!(ex_a2.options & a2::DataSourceOptions::NoAddedRandom));

    auto lo = QString::fromStdString(to_string(listfilter.extractionFilter.filters[0]));
    auto hi = QString::fromStdString(to_string(listfilter.extractionFilter.filters[1]));

    //qDebug() << __PRETTY_FUNCTION__ << "loFilterText before beautifying =" << lo;
    //qDebug() << __PRETTY_FUNCTION__ << "hiFilterText before beautifying =" << hi;

    size_t combinedBits = get_listfilter_combined_bit_count(&ex_a2.listFilter);
    ssize_t loBits = std::min(static_cast<ssize_t>(32), static_cast<ssize_t>(combinedBits));
    ssize_t hiBits = std::max(static_cast<ssize_t>(0), static_cast<ssize_t>(combinedBits) - 32l);

    lo = lo.right(loBits);
    hi = hi.right(hiBits);

    //qDebug() << __PRETTY_FUNCTION__ << "loFilterText after beautifying =" << lo;
    //qDebug() << __PRETTY_FUNCTION__ << "hiFilterText after beautifying =" << hi;

    e.filter_lowWord->setText(lo);
    e.filter_highWord->setText(hi);

    listfilter_editor_update_info_labels(e);
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
    AnalysisServiceProvider *m_serviceProvider;
    QVector<ListFilterExtractorPtr> m_extractors;
    QVector<ListFilterEditor> m_filterEditors;

    QStackedWidget *m_editorStack;
    ListFilterListWidgetUi listWidgetUi;

    QSignalMapper m_nameChangedMapper;
};

ListFilterExtractorDialog::ListFilterExtractorDialog(ModuleConfig *mod, analysis::Analysis *analysis,
                                                     AnalysisServiceProvider *asp, QWidget *parent)
    : ObjectEditorDialog(parent)
    , m_d(std::make_unique<ListFilterExtractorDialogPrivate>())
{
    m_d->m_module = mod;
    m_d->m_analysis = analysis;
    m_d->m_serviceProvider = asp;
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
    auto buttonBox = new QDialogButtonBox(QDialogButtonBox::Ok
                                          | QDialogButtonBox::Apply
                                          | QDialogButtonBox::Cancel);
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
                (void) parent;
                (void) dest;

                //qDebug() << "orig: srcIdx =" << srcIdx << ", dstIdx =" << dstIdx;

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
    m_d->m_extractors = m_d->m_analysis->getListFilterExtractors(
        m_d->m_module->getEventId(), m_d->m_module->getId());

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

    connect(editor.pb_editNameList, &QPushButton::clicked,
            this, [this, ex] ()
            {
                ui::FilterNameListDialog dialog(ex->objectName(), ex->getParameterNames(), this);

                // FIXME (maybe): this immediately applies the changes to the operator
                if (dialog.exec() == QDialog::Accepted)
                    ex->setParameterNames(dialog.getNames());
            });

    updateWordCount();

    return m_d->listWidgetUi.listWidget->count() - 1;
}

void ListFilterExtractorDialog::updateWordCount()
{
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
        m_d->m_analysis->setSourceEdited(ex);
    }

    {
        AnalysisPauser pauser(m_d->m_serviceProvider);
        m_d->m_analysis->setListFilterExtractors(m_d->m_module->getEventId(),
                                                 m_d->m_module->getId(),
                                                 m_d->m_extractors);
        m_d->m_analysis->beginRun(Analysis::KeepState, m_d->m_module->getVMEConfig());
    }

    repopulate();

    emit applied();
}

void ListFilterExtractorDialog::newFilter()
{
    auto ex = std::make_shared<ListFilterExtractor>();
    ex->setObjectName(QSL("new filter"));

#if 1
    auto listFilter = a2::data_filter::make_listfilter(
        a2::data_filter::ListFilter::NoFlag, 1, {
            "XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX",
            "XXXX XXXX XXXX XXXX XXXX XXXX XXXX XXXX" });

            //"1XXX XXXX XXXX XXXX 2XXX XXXX XXXX XXXX",
            //"3XXX XXXX XXXX XXXX 4XXX XXXX XXXX XXXX" });
#else
    auto listFilter = a2::data_filter::make_listfilter(
        a2::data_filter::ListFilter::NoFlag, 1);
#endif

    auto ex_a2 = a2::make_listfilter_extractor(listFilter, 1, 0, a2::DataSourceOptions::NoAddedRandom);

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

void ListFilterExtractorDialog::editListFilterExtractor(const std::shared_ptr<ListFilterExtractor> &lfe)
{
    int idx = m_d->m_extractors.indexOf(lfe);

    if (0 <= idx && idx < m_d->m_extractors.size())
    {
        m_d->listWidgetUi.listWidget->setCurrentRow(idx);
    }
}

QVector<ListFilterExtractorPtr> ListFilterExtractorDialog::getExtractors() const
{
    return m_d->m_extractors;
}

} // namespace analysis
