#pragma once
#include "reorder_editor.h"
#include "ui/common/gutter_column.h"
#include "ui/theme/theme_manager.h"
#include <QPainter>
#include <algorithm>

namespace Kites
{
// Left gutter: "<address>: <hex word>" for each instruction. It is drawn beside the text,
// not inside it, so it is never part of a selection or of the copied code.
class ReorderAddressGutterColumn : public GutterColumn
{
  public:
    explicit ReorderAddressGutterColumn(ReorderEditor *editor)
        : GutterColumn(editor), m_reorderEditor(editor)
    {
    }

    QSize sizeHint() const override
    {
        const QString widest = QString(addressDigits(), QLatin1Char('0')) + QStringLiteral(": 00000000");
        return QSize(fontMetrics().horizontalAdvance(widest) + 16, 0);
    }

  protected:
    void paintEvent(QPaintEvent *event) override
    {
        const ThemeManager &themeManager = ThemeManager::getInstance();
        QPainter painter(this);
        painter.fillRect(event->rect(), themeManager.getEditorBackgroundColor());
        painter.setPen(themeManager.getEditorMutedForegroundColor());

        const std::vector<ReorderedLine> &lines = m_reorderEditor->lines();
        const int digits = addressDigits();

        QTextBlock block = firstVisibleBlock();
        int blockNumber = block.blockNumber();
        int top = blockTop(block);
        int bottom = top + blockHeight(block);

        while (block.isValid() && top <= event->rect().bottom())
        {
            if (block.isVisible() && bottom >= event->rect().top() &&
                blockNumber < static_cast<int>(lines.size()))
            {
                const ReorderedLine &line = lines[static_cast<size_t>(blockNumber)];
                const QString label = QStringLiteral("%1: %2")
                                          .arg(line.address, digits, 16, QLatin1Char('0'))
                                          .arg(line.word, 8, 16, QLatin1Char('0'));
                painter.drawText(8, top, width() - 16, fontMetrics().height(), Qt::AlignLeft, label);
            }

            block = block.next();
            top = bottom;
            bottom = top + blockHeight(block);
            ++blockNumber;
        }
    }

  private:
    int addressDigits() const
    {
        const size_t n = m_reorderEditor->lines().size();
        const quint64 maxAddress = n ? (static_cast<quint64>(n) - 1) * 4 : 0;
        return std::max(4, static_cast<int>(QString::number(maxAddress, 16).length()));
    }

    ReorderEditor *m_reorderEditor;
};
} // namespace Kites
