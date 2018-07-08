#ifndef __LISTFILTER_EXTRACTOR_DIALOG_H__
#define __LISTFILTER_EXTRACTOR_DIALOG_H__

#include <QDialog>
#include "analysis.h"
#include "object_editor_dialog.h"

class MVMEContext;

namespace analysis
{

class ListFilterExtractorDialog: public ObjectEditorDialog
{
    Q_OBJECT
    public:
        ListFilterExtractorDialog(ModuleConfig *mod, analysis::Analysis *analysis,
                                  MVMEContext *context, QWidget *parent = nullptr);
        virtual ~ListFilterExtractorDialog();

        void editListFilterExtractor(const std::shared_ptr<ListFilterExtractor> &lfe);

        QVector<ListFilterExtractorPtr> getExtractors() const;

    public slots:
        virtual void accept() override;
        virtual void reject() override;

        void newFilter();

    private slots:
        void apply();
        void removeFilter();
        void cloneFilter();
        void updateWordCount();

    private:
        void repopulate();
        int addFilterToUi(const ListFilterExtractorPtr &ex);

        struct ListFilterExtractorDialogPrivate;

        std::unique_ptr<ListFilterExtractorDialogPrivate> m_d;
};

}

#endif /* __LISTFILTER_EXTRACTOR_DIALOG_H__ */
