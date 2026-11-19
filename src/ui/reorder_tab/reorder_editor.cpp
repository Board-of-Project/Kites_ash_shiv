#include "reorder_editor.h"
#include "reorder_address_gutter_column.h"
#include "reorder_map_gutter_column.h"
#include "ui/common/line_number_gutter_column.h"
#include "ui/common/syntax_highlighter.h"
#include <QPainter>
#include <QResizeEvent>
#include <QStringList>
#include <QTextBlock>

namespace Kites
{
ReorderEditor::ReorderEditor(QWidget *parent) : KitesEditor(parent)
{
    m_syntaxHighlighter = new SyntaxHighlighter(document());
    setReadOnly(true);
    // read-only, but the text can still be selected with the mouse or keyboard and copied
    setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
    setLineWrapMode(QPlainTextEdit::NoWrap);

    addLeftGutterColumn(new LineNumberGutterColumn(this));
    addLeftGutterColumn(new ReorderAddressGutterColumn(this));
    addRightGutterColumn(new ReorderMapGutterColumn(this));
    relayoutGutters();
}

void ReorderEditor::setLines(const std::vector<ReorderedLine> &lines)
{
    m_lines = lines;

    QStringList texts;
    texts.reserve(static_cast<int>(lines.size()));
    for (const ReorderedLine &line : lines)
        texts << line.text;
    setPlainText(texts.join(QLatin1Char('\n'))); // instruction text only: address, hex and
                                                 // indices live in the gutters
    relayoutGutters(); // gutter widths depend on how many digits the numbers need
}

void ReorderEditor::clearLines()
{
    m_lines.clear();
    clear();
    relayoutGutters();
}

QString ReorderEditor::codeText(bool withIndices) const
{
    QString out;
    for (size_t k = 0; k < m_lines.size(); ++k)
    {
        out += m_lines[k].text;
        if (withIndices)
            out += QStringLiteral("   # orig #%1 -> new #%2").arg(m_lines[k].from).arg(static_cast<int>(k));
        out += QLatin1Char('\n');
    }
    return out;
}

void ReorderEditor::paintEvent(QPaintEvent *event)
{
    QPlainTextEdit::paintEvent(event);

    // Tint the lines whose instruction moved. This is painted here instead of with extra
    // selections because KitesEditor::highlightCurrentLine() replaces all extra selections
    // whenever the cursor moves, which would erase the tint as soon as you click in the text.
    QPainter painter(viewport());
    const QColor tint(80, 140, 230, 40);

    QTextBlock block = firstVisibleBlock();
    while (block.isValid())
    {
        const QRectF rect = blockBoundingGeometry(block).translated(contentOffset());
        if (rect.top() > event->rect().bottom())
            break;

        const size_t k = static_cast<size_t>(block.blockNumber());
        if (block.isVisible() && rect.bottom() >= event->rect().top() && k < m_lines.size() &&
            m_lines[k].from != static_cast<int>(k))
        {
            painter.fillRect(QRectF(0, rect.top(), viewport()->width(), rect.height()), tint);
        }
        block = block.next();
    }
}

void ReorderEditor::relayoutGutters()
{
    updateViewPortMargins();
    // KitesEditor only positions the gutters in resizeEvent, so re-run it after widths change
    QResizeEvent e(size(), size());
    resizeEvent(&e);
    viewport()->update();
}
} // namespace Kites
