#pragma once
#include "ui/common/kitestab.h"
#include <QCheckBox>
#include <QGroupBox>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTableWidget>

namespace Kites
{
class ReorderTab : public KitesTab
{
    Q_OBJECT
  public:
    explicit ReorderTab(QWidget *parent = nullptr);
    bool executeReordered() const { return m_exec->isChecked(); }

  signals:
    void exportRequested();

  public slots:
    // key identifies "this program on this processor"; a new key discards old measurements
    void setReordered(const QString &listing, const QString &key, int cyclesBefore,
                      int cyclesAfter, int stallsBefore, int stallsAfter, bool weighted);
    void setMeasured(bool reordered, unsigned cycles, unsigned stalls, unsigned retired);
    void clear();

  private:
    struct Measured
    {
        bool valid = false;
        unsigned cycles = 0, stalls = 0, retired = 0;
    };
    void refreshMeasured();
    void resetTable();

    QCheckBox *m_exec;
    QPushButton *m_export;
    QLabel *m_model;
    QTableWidget *m_table;
    QLabel *m_note;
    QLabel *m_prediction;
    QPlainTextEdit *m_view;

    QString m_key;
    bool m_enabled = false;
    bool m_weighted = false;
    int m_stallsBefore = 0, m_stallsAfter = 0;
    Measured m_orig, m_reord;
};
} // namespace Kites