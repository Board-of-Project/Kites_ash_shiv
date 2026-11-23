#include "reordertab.h"
#include "reordering/cycle_report.h"
#include <QClipboard>
#include <QGuiApplication>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLocale>
#include <QScrollBar>
#include <QSplitter>
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

    // One-line result summary shown above everything else.
    m_headline = new QLabel(this);
    m_headline->setWordWrap(true);
    QFont headlineFont = m_headline->font();
    headlineFont.setBold(true);
    headlineFont.setPointSizeF(headlineFont.pointSizeF() * 1.25);
    m_headline->setFont(headlineFont);
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

    m_note = new QLabel("*Other = pipeline fill + branch/jump bubbles; reordering does not change it. "
                        "The model estimate above counts data stalls only, so its totals differ from "
                        "the measured totals; compare the savings, not the totals.",
                        measBox);
    m_note->setWordWrap(true);
    m_prediction = new QLabel(measBox);
    m_prediction->setWordWrap(true);
    m_prediction->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto *measLayout = new QVBoxLayout(measBox);
    measLayout->addWidget(m_table);
    measLayout->addWidget(m_note);
    measLayout->addWidget(m_prediction);

    // --- code listing ---
    auto *codeBox = new QGroupBox("Original vs. reordered code", this);
    m_view = new ReorderEditor(codeBox, ReorderEditor::Role::Reordered);
    m_origView = new ReorderEditor(codeBox, ReorderEditor::Role::Original);
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

    // Original (left) and reordered (right) listings side by side. Rows that moved are tinted in
    // both; the right gutters give "#original -> #new" and how far the instruction moved.
    auto *panes = new QSplitter(Qt::Horizontal, codeBox);
    panes->setChildrenCollapsible(false);
    auto addPane = [panes](const QString &title, ReorderEditor *editor) {
        auto *pane = new QWidget(panes);
        auto *paneLayout = new QVBoxLayout(pane);
        paneLayout->setContentsMargins(0, 0, 0, 0);
        paneLayout->addWidget(new QLabel(title, pane));
        paneLayout->addWidget(editor, 1);
        panes->addWidget(pane);
    };
    addPane("Original", m_origView);
    addPane("Reordered", m_view);
    panes->setStretchFactor(0, 1);
    panes->setStretchFactor(1, 1);
    codeLayout->addWidget(panes, 1);

    // Both listings have the same number of rows, so keep them scrolled together.
    connect(m_view->verticalScrollBar(), &QScrollBar::valueChanged, m_origView->verticalScrollBar(),
            &QScrollBar::setValue);
    connect(m_origView->verticalScrollBar(), &QScrollBar::valueChanged, m_view->verticalScrollBar(),
            &QScrollBar::setValue);

    // --- page layout ---
    auto *top = new QHBoxLayout;
    top->addWidget(m_exec);
    top->addStretch();
    top->addWidget(m_export);

    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_headline);
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
                              int cyclesAfter, int stallsBefore, int stallsAfter, bool weighted,
                              const std::vector<ReorderedLine> &original)
{
    if (key != m_key) // different program or processor: old measurements no longer apply
    {
        m_orig = Measured{};
        m_reord = Measured{};
        m_key = key;
    }
    m_enabled = true;
    m_weighted = weighted;
    m_cyclesBefore = cyclesBefore;
    m_cyclesAfter = cyclesAfter;
    m_stallsBefore = stallsBefore;
    m_stallsAfter = stallsAfter;
    m_view->setLines(lines);
    m_origView->setLines(original);
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
    m_origView->clearLines();
    m_copy->setEnabled(false);
    m_copyWithIndices->setEnabled(false);
    m_model->setText("Reordering not available for this processor");
    m_headline->clear();
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
    refreshHeadline();
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
        // both versions measured: the table shows the real result; compare it with the model
        const long long modelSaved = static_cast<long long>(m_cyclesBefore) - m_cyclesAfter;
        const long long measuredSaved =
            static_cast<long long>(m_orig.cycles) - static_cast<long long>(m_reord.cycles);
        m_prediction->setText(
            QString("Model check: model predicted %1 cycles saved, Kites measured %2.%3")
                .arg(num(modelSaved), num(measuredSaved),
                     m_weighted ? QString() : QString(" (model counts loops once)")));
    }
}

void ReorderTab::refreshHeadline()
{
    if (!m_enabled)
    {
        m_headline->clear();
        return;
    }
    const auto &lines = m_view->lines();
    int moved = 0;
    for (size_t k = 0; k < lines.size(); ++k)
        if (lines[k].from != static_cast<int>(k))
            ++moved;
    const QString movedText = QString("%1 of %2 instructions moved").arg(moved).arg(static_cast<int>(lines.size()));
    auto percent = [](long long saved, long long base) {
        return QString::number(base > 0 ? 100.0 * saved / base : 0.0, 'f', 1);
    };

    if (m_orig.valid && m_reord.valid)
    {
        const long long saved = static_cast<long long>(m_orig.cycles) - m_reord.cycles;
        if (saved > 0)
            m_headline->setText(QString("Reordering saved %1 cycles (%2%)  |  data stalls %3 → %4  |  %5")
                                    .arg(num(saved), percent(saved, m_orig.cycles),
                                         num(m_orig.stalls), num(m_reord.stalls), movedText));
        else if (saved == 0)
            m_headline->setText(QString("No change in cycles  |  %1").arg(movedText));
        else
            m_headline->setText(QString("Reordered code is slower by %1 cycles  |  %2")
                                    .arg(num(-saved), movedText));
    }
    else
    {
        const long long saved = static_cast<long long>(m_cyclesBefore) - m_cyclesAfter;
        if (saved > 0)
            m_headline->setText(QString("Model predicts %1 cycles saved (%2%)  |  %3  |  run to confirm")
                                    .arg(num(saved), percent(saved, m_cyclesBefore), movedText));
        else
            m_headline->setText(QString("Model found no improvement; original order kept  |  %1")
                                    .arg(movedText));
    }
}
} // namespace Kites