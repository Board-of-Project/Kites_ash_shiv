#pragma once
#include "ui/common/kites_editor.h"
#include <QString>
#include <QtGlobal>
#include <vector>

namespace Kites
{
class SyntaxHighlighter;

// One instruction of the reordered program.
struct ReorderedLine
{
    int from = 0;        // original instruction number (0-based)
    quint64 address = 0; // address in the reordered program (slot * 4)
    quint32 word = 0;    // machine code
    QString text;        // assembly, e.g. "add x1, x2, x3"
};

// Read-only view of the reordered program, laid out like the Editor / Profiler tabs:
//
//   left gutters : line number | address + hex word          (not part of the text)
//   text         : the instruction only, syntax highlighted  (what you select / copy)
//   right gutter : "#from -> #to" = original -> new instruction number
//
// Because the document holds only the assembly text, selecting it and pressing Ctrl+C, or
// codeText(), gives code that can be pasted straight into the Editor tab.
class ReorderEditor : public KitesEditor
{
  public:
    explicit ReorderEditor(QWidget *parent = nullptr);

    void setLines(const std::vector<ReorderedLine> &lines);
    void clearLines();
    const std::vector<ReorderedLine> &lines() const
    {
        return m_lines;
    }

    // The reordered code as plain assembly, one instruction per line.
    // withIndices appends "   # orig #5 -> new #2" to every line ('#' starts a comment, so the
    // result still assembles).
    QString codeText(bool withIndices) const;

  protected:
    void paintEvent(QPaintEvent *event) override;

  private:
    void relayoutGutters();

    SyntaxHighlighter *m_syntaxHighlighter = nullptr;
    std::vector<ReorderedLine> m_lines;
};
} // namespace Kites
