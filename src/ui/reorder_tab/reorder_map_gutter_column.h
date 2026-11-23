#pragma once
#include "reorder_editor.h"
#include "ui/common/gutter_column.h"
#include "ui/theme/theme_manager.h"
#include <QPainter>
#include <algorithm>

namespace Kites
{
// Right gutter: "#from -> #to" for every instruction, where "from" is the instruction's number in
// the original program and "to" its number in the reordered one. Unmoved instructions are muted,
// moved ones are drawn at full strength.
class ReorderMapGutterColumn : public GutterColumn
{
  public:
    explicit ReorderMapGutterColumn(ReorderEditor *editor)
        : GutterColumn(editor), m_reorderEditor(editor)
    {
    }

    QSize sizeHint() const override
    {
        const int n = static_cast<int>(m_reorderEditor->lines().size());
        const QString nines(QString::number(std::max(n, 1)).length(), QLatin1Char('9'));
        const QString widest = QStringLiteral("#%1 %2 #%1 %3%1").arg(nines, arrow(), up());
        return QSize(fontMetrics().horizontalAdvance(widest) + 16, 0);
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        const ThemeManager &themeManager = ThemeManager::getInstance();
        QPainter painter(this);
        painter.fillRect(event->rect(), themeManager.getEditorBackgroundColor());

        const std::vector<ReorderedLine> &lines = m_reorderEditor->lines();

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = blockTop(block);
        int bottom = top + blockHeight(block);

        while (block.isValid() && top <= event->rect().bottom())
        {
            if (block.isVisible() && bottom >= event->rect().top() &&
                blockNumber < static_cast<int>(lines.size()))
            {
                const int other = lines[static_cast<size_t>(blockNumber)].from;
                // Reordered view: row = new number, other = original number.
                // Original view : row = original number, other = new number.
                const bool originalView = m_reorderEditor->role() == ReorderEditor::Role::Original;
                const int from = originalView ? blockNumber : other;
                const int to = originalView ? other : blockNumber;
                painter.setPen(from == to ? themeManager.getEditorMutedForegroundColor()
                                          : themeManager.getEditorForegroundColor());
                QString label = QStringLiteral("#%1 %2 #%3").arg(from).arg(arrow()).arg(to);
                if (from > to) // pulled earlier in the program
                    label += QStringLiteral(" %1%2").arg(up()).arg(from - to);
                else if (from < to) // pushed later
                    label += QStringLiteral(" %1%2").arg(down()).arg(to - from);
                painter.drawText(8, top, width() - 16, fontMetrics().height(), Qt::AlignLeft, label);
            }

            block = block.next();
            top = bottom;
            bottom = top + blockHeight(block);
            ++blockNumber;
        }
    }

  private:
    static QString up()
    {
        return QString(QChar(0x25B2)); // black up-pointing triangle
    }
    static QString down()
    {
        return QString(QChar(0x25BC)); // black down-pointing triangle
    }
    static QString arrow()
    {
        return QString(QChar(0x2192)); // right arrow
    }

    ReorderEditor *m_reorderEditor;
};
} // namespace Kites
