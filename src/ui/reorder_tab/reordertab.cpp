#include "reordertab.h"
#include "reordering/cycle_report.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QTimer>
#include <QVBoxLayout>

namespace Kites
{
namespace
{
constexpr int kRows = 3; // Original, Reordered, Saved
constexpr int kCols = 5; // Cycles, Instructions, Data stalls, Other, CPI

QString num(long long v) { return QLocale().toString(v); }
QString signedNum(long long v) { return QString(v > 0 ? "+" : "") + num(v); }
QString dash() { return QStringLiteral("—"); }
} // namespace

ReorderTab::ReorderTab(QWidget *parent) : KitesTab(parent)
{
    m_exec = new QCheckBox("Execute reordered code on next Run", this);
    m_export = new QPushButton("Export test case...", this);

    // --- model summary ---
    auto *modelBox = new QGroupBox("Estimate (model)", this);
    m_model = new QLabel(modelBox);
    m_model->setWordWrap(true);
    auto *modelLayout = new QVBoxLayout(modelBox);
    modelLayout->addWidget(m_model);

    // --- measured table ---
    auto *measBox = new QGroupBox("Measured by Kites (last completed run)", this);
    m_table = new QTableWidget(kRows, kCols, measBox);
    m_table->setHorizontalHeaderLabels({"Cycles", "Instructions", "Data stalls", "Other*", "CPI"});
    m_table->setVerticalHeaderLabels({"Original", "Reordered", "Saved"});
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_table->setSelectionMode(QAbstractItemView::NoSelection);
    m_table->setFocusPolicy(Qt::NoFocus);
    m_table->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_table->horizontalHeader()->setSectionResizeMode(QHeaderView::Stretch);
    QFont bold = m_table->font();
    bold.setBold(true);
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c)
        {
            auto *item = new QTableWidgetItem(dash());
            item->setTextAlignment(Qt::AlignRight | Qt::AlignVCenter);
            if (r == 2)
                item->setFont(bold);
            m_table->setItem(r, c, item);
        }
    m_table->setFixedHeight(m_table->horizontalHeader()->sizeHint().height() +
                            kRows * m_table->rowHeight(0) + 2 * m_table->frameWidth() + 2);

    m_note = new QLabel("*Other = pipeline fill + branch/jump bubbles", measBox);
    m_note->setWordWrap(true);
    m_prediction = new QLabel(measBox);
    m_prediction->setWordWrap(true);
    m_prediction->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *measLayout = new QVBoxLayout(measBox);
    measLayout->addWidget(m_table);
    measLayout->addWidget(m_note);
    measLayout->addWidget(m_prediction);

    // --- code listing ---
    auto *codeBox = new QGroupBox("Reordered code", this);
    m_view = new ReorderEditor(codeBox);
    m_copy = new QPushButton("Copy code", codeBox);
    m_copy->setToolTip("Copy the reordered instructions only: no address, hex word or indices");
    m_copyWithIndices = new QPushButton("Copy with indices", codeBox);
    m_copyWithIndices->setToolTip("Copy the instructions, each followed by a comment  # orig #5 -> new #2");
    auto *copyBar = new QHBoxLayout;
    copyBar->addWidget(new QLabel("Select text to copy part of it, or:", codeBox));
    copyBar->addStretch();
    copyBar->addWidget(m_copy);
    copyBar->addWidget(m_copyWithIndices);
    auto *codeLayout = new QVBoxLayout(codeBox);
    codeLayout->addLayout(copyBar);
    codeLayout->addWidget(m_view);

    // --- page layout ---
    auto *top = new QHBoxLayout;
    top->addWidget(m_exec);
    top->addStretch();
    top->addWidget(m_export);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(modelBox);
    layout->addWidget(measBox);
    layout->addWidget(codeBox, 1); // the listing takes the remaining height

    connect(m_export, &QPushButton::clicked, this, &ReorderTab::exportRequested);
    connect(m_copy, &QPushButton::clicked, this, [this] { copyCode(m_copy, false); });
    connect(m_copyWithIndices, &QPushButton::clicked, this,
            [this] { copyCode(m_copyWithIndices, true); });
    clear();
}

void ReorderTab::setReordered(const std::vector<ReorderedLine> &lines, const QString &key, int cyclesBefore,
                              int cyclesAfter, int stallsBefore, int stallsAfter, bool weighted)
{
    if (key != m_key) // different program or processor: old measurements no longer apply
    {
        m_orig = Measured{};
        m_reord = Measured{};
        m_key = key;
    }
    m_enabled = true;
    m_weighted = weighted;
    m_stallsBefore = stallsBefore;
    m_stallsAfter = stallsAfter;
    m_view->setLines(lines);
    m_copy->setEnabled(!lines.empty());
    m_copyWithIndices->setEnabled(!lines.empty());
    m_model->setText(QString("Cycles %1 → %2 (saved %3)   |   Data stalls %4 → %5   |   %6")
                         .arg(num(cyclesBefore), num(cyclesAfter), num(cyclesBefore - cyclesAfter),
                              num(stallsBefore), num(stallsAfter),
                              weighted ? "weighted by executed counts"
                                       : "one pass, loops counted once"));
    refreshMeasured();
}

void ReorderTab::setMeasured(bool reordered, unsigned cycles, unsigned stalls, unsigned retired)
{
    if (!m_enabled)
        return;
    (reordered ? m_reord : m_orig) = Measured{true, cycles, stalls, retired};
    refreshMeasured();
}

void ReorderTab::clear()
{
    m_enabled = false;
    m_weighted = false;
    m_key.clear();
    m_orig = Measured{};
    m_reord = Measured{};
    m_view->clearLines();
    m_copy->setEnabled(false);
    m_copyWithIndices->setEnabled(false);
    m_model->setText("Reordering not available for this processor");
    resetTable();
    m_prediction->clear();
}

void ReorderTab::copyCode(QPushButton *button, bool withIndices)
{
    QGuiApplication::clipboard()->setText(m_view->codeText(withIndices));
    const QString original = button->text(); // brief confirmation on the button itself
    button->setText("Copied");
    QTimer::singleShot(1200, button, [button, original] { button->setText(original); });
}

void ReorderTab::resetTable()
{
    for (int r = 0; r < kRows; ++r)
        for (int c = 0; c < kCols; ++c)
            m_table->item(r, c)->setText(dash());
}

void ReorderTab::refreshMeasured()
{
    auto fillRow = [this](int row, const Measured &m) {
        if (!m.valid)
        {
            for (int c = 0; c < kCols; ++c)
                m_table->item(row, c)->setText(dash());
            return;
        }
        const long long other = static_cast<long long>(m.cycles) - m.retired - m.stalls;
        const double cpi = m.retired ? double(m.cycles) / m.retired : 0.0;
        m_table->item(row, 0)->setText(num(m.cycles));
        m_table->item(row, 1)->setText(num(m.retired));
        m_table->item(row, 2)->setText(num(m.stalls));
        m_table->item(row, 3)->setText(num(other));
        m_table->item(row, 4)->setText(QString::number(cpi, 'f', 2));
    };
    fillRow(0, m_orig);
    fillRow(1, m_reord);

    if (m_orig.valid && m_reord.valid) // "Saved" row = original minus reordered
    {
        auto diff = [](unsigned a, unsigned b) { return static_cast<long long>(a) - b; };
        const long long otherO = static_cast<long long>(m_orig.cycles) - m_orig.retired - m_orig.stalls;
        const long long otherR = static_cast<long long>(m_reord.cycles) - m_reord.retired - m_reord.stalls;
        const double cpiO = m_orig.retired ? double(m_orig.cycles) / m_orig.retired : 0.0;
        const double cpiR = m_reord.retired ? double(m_reord.cycles) / m_reord.retired : 0.0;
        m_table->item(2, 0)->setText(signedNum(diff(m_orig.cycles, m_reord.cycles)));
        m_table->item(2, 1)->setText(signedNum(diff(m_orig.retired, m_reord.retired)));
        m_table->item(2, 2)->setText(signedNum(diff(m_orig.stalls, m_reord.stalls)));
        m_table->item(2, 3)->setText(signedNum(otherO - otherR));
        m_table->item(2, 4)->setText(QString(cpiO - cpiR > 0 ? "+" : "") +
                                     QString::number(cpiO - cpiR, 'f', 2));
    }
    else
    {
        for (int c = 0; c < kCols; ++c)
            m_table->item(2, c)->setText(dash());
    }

    // --- model check and prediction ---
    if (!m_orig.valid && !m_reord.valid)
    {
        m_prediction->setText("Run the program to see measured values.");
    }
    else if (m_orig.valid != m_reord.valid) // exactly one version has been run
    {
        const bool ranReordered = m_reord.valid;
        if (!m_weighted)
        {
            m_prediction->setText("Model check unavailable: executed counts did not match the "
                                  "retired instructions for this run.");
        }
        else
        {
            const Measured &m = ranReordered ? m_reord : m_orig;
            const MeasuredStats ran{m.cycles, m.retired, m.stalls};
            const long long modelRan = ranReordered ? m_stallsAfter : m_stallsBefore;
            const long long modelOther = ranReordered ? m_stallsBefore : m_stallsAfter;
            const long long predicted = predictOtherCycles(ran, modelRan, modelOther);
            const long long delta = predicted - static_cast<long long>(m.cycles);
            m_prediction->setText(
                QString("Model check: model stalls %1 vs measured %2 (difference %3)\n"
                        "Predicted %4 total: %5 cycles (%6)")
                    .arg(num(modelRan), num(m.stalls), signedNum(static_cast<long long>(m.stalls) - modelRan),
                         ranReordered ? "original" : "reordered", num(predicted),
                         delta == 0 ? QString("no change")
                                    : QString("%1 %2 cycles").arg(delta < 0 ? "saves" : "costs").arg(num(std::llabs(delta)))));
        }
    }
    else
    {
        m_prediction->clear(); // both versions measured: the table shows the real result
    }
}
} // namespace Kites