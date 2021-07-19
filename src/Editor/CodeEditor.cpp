/*
 * Copyright (C) 2019-2020 Ashar Khan <ashar786khan@gmail.com>
 *
 * This file is part of CP Editor.
 *
 * CP Editor is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * I will not be responsible if CP Editor behaves in unexpected way and
 * causes your ratings to go down and or lose any important contest.
 *
 * Believe Software is "Software" and it isn't immune to bugs.
 *
 */

/* Some codes are from the examples of KDE/syntax-highlighting, which are licensed under the MIT License:

    Copyright (C) 2016 Volker Krause <vkrause@kde.org>

    Permission is hereby granted, free of charge, to any person obtaining
    a copy of this software and associated documentation files (the
    "Software"), to deal in the Software without restriction, including
    without limitation the rights to use, copy, modify, merge, publish,
    distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject to
    the following conditions:

    The above copyright notice and this permission notice shall be included
    in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
    CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
*/

#include "Editor/CodeEditor.hpp"
#include "Core/EventLogger.hpp"
#include "Editor/CodeEditorSideBar.hpp"
#include "Editor/KSHRepository.hpp"
#include "Settings/SettingsManager.hpp"
#include "generated/SettingsHelper.hpp"
#include <QApplication>
#include <QFontDatabase>
#include <QMimeData>
#include <QPainter>
#include <QRegularExpression>
#include <QRgb>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextCharFormat>
#include <QTextStream>
#include <QToolTip>
#include <definition.h>
#include <syntaxhighlighter.h>

namespace Editor
{
CodeEditor::CodeEditor(QWidget *widget) : QPlainTextEdit(widget)
{
    highlighter = new KSyntaxHighlighting::SyntaxHighlighter(document());
    sideBar = new CodeEditorSidebar(this);

    connect(document(), &QTextDocument::blockCountChanged, this, &CodeEditor::updateBottomMargin);
    connect(document(), &QTextDocument::blockCountChanged, this, &CodeEditor::updateSidebarGeometry);
    connect(this, &QPlainTextEdit::updateRequest, this, &CodeEditor::updateSidebarArea);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightCurrentLine);
    connect(this, &QPlainTextEdit::cursorPositionChanged, this, &CodeEditor::highlightParentheses);
    connect(this, &QPlainTextEdit::selectionChanged, this, &CodeEditor::highlightOccurrences);

    setMouseTracking(true);
}

void CodeEditor::applySettings(const QString &lang)
{
    LOG_INFO("Applying settings to a CodeEditor");

    language = lang;

    m_tabReplace = QString(SettingsHelper::getTabWidth(), ' ');
    setTabStopDistance(fontMetrics().horizontalAdvance(QString(SettingsHelper::getTabWidth() * 200, ' ')) / 200.0);

    setFont(SettingsHelper::getCodeEditorFont());

    if (SettingsHelper::isWrapText())
        setWordWrapMode(QTextOption::WordWrap);
    else
        setWordWrapMode(QTextOption::NoWrap);

    setHighlightCurrentLine(SettingsHelper::isHighlightCurrentLine());

    updateBottomMargin();

    if (language.isEmpty())
        return;

    highlighter->setDefinition(
        KSyntaxHighlightingRepository::getSyntaxHighlightingRepository()->definitionForName(language));

    if (SettingsHelper::getCodeEditorTheme() == "Default")
    {
        setTheme(QColor(theme.editorColor(KSyntaxHighlighting::Theme::BackgroundColor)).lightness() < 128
                     ? KSyntaxHighlightingRepository::getSyntaxHighlightingRepository()->defaultTheme(
                           KSyntaxHighlighting::Repository::DarkTheme)
                     : KSyntaxHighlightingRepository::getSyntaxHighlightingRepository()->defaultTheme(
                           KSyntaxHighlighting::Repository::LightTheme));
    }
    else
    {
        setTheme(KSyntaxHighlightingRepository::getSyntaxHighlightingRepository()->theme(
            SettingsHelper::getCodeEditorTheme()));
    }
    if (vimCursor())
        return;

    parentheses.clear();

    auto list = SettingsManager::get(language + "/Parentheses").toList();

    for (auto const &var : list)
    {
        auto li = var.toList();
        if (li.length() != 5)
        {
            LOG_ERR(INFO_OF(li.length()));
            continue;
        }

        auto left = li[0].toChar();
        auto right = li[1].toChar();

        auto getFlag = [](Qt::CheckState state, bool def) {
            switch (state)
            {
            case Qt::Checked:
                return true;
            case Qt::PartiallyChecked:
                return def;
            case Qt::Unchecked:
                return false;
            default:
                Q_UNREACHABLE();
            }
        };

        bool autoComplete = getFlag(Qt::CheckState(li[2].toInt()), SettingsHelper::isAutoCompleteParentheses());
        bool autoRemove = getFlag(Qt::CheckState(li[3].toInt()), SettingsHelper::isAutoRemoveParentheses());
        bool tabJumpOut = getFlag(Qt::CheckState(li[4].toInt()), SettingsHelper::isTabJumpOutParentheses());

        parentheses.push_back({left, right, autoComplete, autoRemove, tabJumpOut});
    }
}

void CodeEditor::setTheme(const KSyntaxHighlighting::Theme &newTheme)
{
    theme = newTheme;

    if (theme.isValid())
    {
        QRgb backgroundColor = theme.editorColor(KSyntaxHighlighting::Theme::BackgroundColor);
        QRgb selectionBackground = theme.editorColor(KSyntaxHighlighting::Theme::TextSelection);
        QRgb textColor = theme.textColor(KSyntaxHighlighting::Theme::Normal);

        setStyleSheet(QString("QPlainTextEdit { background-color: %1; selection-background-color: %2; color: %3; }")
                          .arg(QColor(backgroundColor).name())
                          .arg(QColor(selectionBackground).name())
                          .arg(QColor(textColor).name()));
    }

    highlighter->setTheme(theme);
    highlighter->rehighlight();
    highlightCurrentLine();
    highlightOccurrences();
    highlightParentheses();
}

int CodeEditor::sidebarWidth() const
{
    int digits = 1;
    auto count = document()->blockCount();
    while (count >= 10)
    {
        ++digits;
        count /= 10;
    }
    return 4 + fontMetrics().horizontalAdvance(QLatin1Char('9')) * digits + fontMetrics().lineSpacing();
}

void CodeEditor::sidebarPaintEvent(QPaintEvent *event)
{
    QPainter painter(sideBar);
    painter.fillRect(event->rect(), highlighter->theme().editorColor(KSyntaxHighlighting::Theme::IconBorder));

    auto block = firstVisibleBlock();
    auto blockNumber = block.blockNumber();
    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + static_cast<int>(blockBoundingRect(block).height());
    const int currentBlockNumber = textCursor().blockNumber();

    const auto foldingMarkerSize = fontMetrics().lineSpacing();

    while (block.isValid() && top <= event->rect().bottom())
    {
        if (block.isVisible() && bottom >= event->rect().top())
        {
            const auto number = QString::number(blockNumber + 1);
            painter.setPen(highlighter->theme().editorColor((blockNumber == currentBlockNumber)
                                                                ? KSyntaxHighlighting::Theme::CurrentLineNumber
                                                                : KSyntaxHighlighting::Theme::LineNumbers));
            painter.drawText(0, top, sideBar->width() - 2 - foldingMarkerSize, fontMetrics().height(), Qt::AlignRight,
                             number);
        }

        // folding marker
        if (block.isVisible() && isFoldable(block))
        {
            QPolygonF polygon;
            if (isFolded(block))
            {
                polygon << QPointF(foldingMarkerSize * 0.4, foldingMarkerSize * 0.25);
                polygon << QPointF(foldingMarkerSize * 0.4, foldingMarkerSize * 0.75);
                polygon << QPointF(foldingMarkerSize * 0.8, foldingMarkerSize * 0.5);
            }
            else
            {
                polygon << QPointF(foldingMarkerSize * 0.25, foldingMarkerSize * 0.4);
                polygon << QPointF(foldingMarkerSize * 0.75, foldingMarkerSize * 0.4);
                polygon << QPointF(foldingMarkerSize * 0.5, foldingMarkerSize * 0.8);
            }
            painter.save();
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setPen(Qt::NoPen);
            painter.setBrush(QColor(highlighter->theme().editorColor(KSyntaxHighlighting::Theme::CodeFolding)));
            painter.translate(sideBar->width() - foldingMarkerSize, top);
            painter.drawPolygon(polygon);
            painter.restore();
        }

        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
        ++blockNumber;
    }
}

void CodeEditor::updateSidebarGeometry()
{
    setViewportMargins(sidebarWidth(), 0, 0, 0);
    const auto r = contentsRect();
    sideBar->setGeometry(QRect(r.left(), r.top(), sidebarWidth(), r.height()));
}

void CodeEditor::updateSidebarArea(const QRect &rect, int dy)
{
    if (dy)
        sideBar->scroll(0, dy);
    else
        sideBar->update(0, rect.y(), sideBar->width(), rect.height());
}

QTextBlock CodeEditor::blockAtPosition(int y) const
{
    auto block = firstVisibleBlock();
    if (!block.isValid())
        return QTextBlock();

    int top = static_cast<int>(blockBoundingGeometry(block).translated(contentOffset()).top());
    int bottom = top + static_cast<int>(blockBoundingRect(block).height());
    do
    {
        if (top <= y && y <= bottom)
            return block;
        block = block.next();
        top = bottom;
        bottom = top + static_cast<int>(blockBoundingRect(block).height());
    } while (block.isValid());
    return QTextBlock();
}

bool CodeEditor::isFoldable(const QTextBlock &block) const
{
    return highlighter->startsFoldingRegion(block);
}

bool CodeEditor::isFolded(const QTextBlock &block)
{
    if (!block.isValid())
        return false;
    const auto nextBlock = block.next();
    if (!nextBlock.isValid())
        return false;
    return !nextBlock.isVisible();
}

void CodeEditor::toggleFold(const QTextBlock &startBlock)
{
    // we also want to fold the last line of the region, therefore the ".next()"
    const auto endBlock = highlighter->findFoldingRegionEnd(startBlock).next();

    if (isFolded(startBlock))
    {
        // unfold
        auto block = startBlock.next();
        while (block.isValid() && !block.isVisible())
        {
            block.setVisible(true);
            block.setLineCount(block.layout()->lineCount());
            block = block.next();
        }
    }
    else
    {
        // fold
        auto block = startBlock.next();
        while (block.isValid() && block != endBlock)
        {
            block.setVisible(false);
            block.setLineCount(0);
            block = block.next();
        }
    }

    // redraw document
    document()->markContentsDirty(startBlock.position(), endBlock.position() - startBlock.position() + 1);

    // update scrollbars
    emit document()->documentLayout()->documentSizeChanged(document()->documentLayout()->documentSize());
}

void CodeEditor::resizeEvent(QResizeEvent *e)
{
    QPlainTextEdit::resizeEvent(e);
    updateSidebarGeometry();
    updateBottomMargin();
}

void CodeEditor::changeEvent(QEvent *e)
{
    QPlainTextEdit::changeEvent(e);
    if (e->type() == QEvent::FontChange)
        updateBottomMargin();
}

void CodeEditor::paintEvent(QPaintEvent *e)
{
    if (m_vimCursor)
    {
        if (!m_cursorRect.isNull() && e->rect().intersects(m_cursorRect))
        {
            QRect rect = m_cursorRect;
            m_cursorRect = QRect();
            viewport()->update(rect);
        }

        // Draw text cursor.
        QRect rect = cursorRect();
        if (e->rect().intersects(rect))
        {
            QPainter painter(viewport());

            if (overwriteMode())
            {
                QFontMetrics fm(font());
                const int position = textCursor().position();
                const QChar c = document()->characterAt(position);
                rect.setWidth(fm.horizontalAdvance(c));
                painter.setPen(Qt::NoPen);

                painter.setBrush(QColor(highlighter->theme().textColor(KSyntaxHighlighting::Theme::Char)));
                painter.setCompositionMode(QPainter::CompositionMode_Difference);
            }
            else
            {
                rect.setWidth(cursorWidth());
                painter.setPen(QColor(highlighter->theme().textColor(KSyntaxHighlighting::Theme::Char)));
            }

            painter.drawRect(rect);
            m_cursorRect = rect;
        }
    }
    QPlainTextEdit::paintEvent(e);
}
void CodeEditor::focusOutEvent(QFocusEvent *e)
{
    if (m_vimCursor)
    {
        setOverwriteMode(true); // makes a block cursor when focus is lost
    }
    QPlainTextEdit::focusOutEvent(e);
}

void CodeEditor::setHighlightCurrentLine(bool enabled)
{
    m_highlightingCurrentLine = enabled;
}

void CodeEditor::setVimCursor(bool value)
{
    m_vimCursor = value;

    setOverwriteMode(false);
    setCursorWidth(value ? 0 : 1);
}

bool CodeEditor::isHighlightingCurrentLine() const
{
    return m_highlightingCurrentLine;
}

bool CodeEditor::vimCursor() const
{
    return m_vimCursor;
}

void CodeEditor::wheelEvent(QWheelEvent *e)
{
    if (e->modifiers() == Qt::ControlModifier)
    {
        const auto sizes = QFontDatabase::standardSizes();
        if (sizes.isEmpty())
        {
            LOG_ERR("QFontDatabase::standardSizes() is empty");
            return;
        }
        int newSize = font().pointSize();
        if (e->angleDelta().y() > 0)
            newSize = qMin(newSize + 1, sizes.last());
        else if (e->angleDelta().y() < 0)
            newSize = qMax(newSize - 1, sizes.first());
        if (newSize != font().pointSize())
        {
            QFont newFont = font();
            newFont.setPointSize(newSize);
            setFont(newFont);
            emit fontChanged(newFont);
        }
    }
    else
        QPlainTextEdit::wheelEvent(e);
}

void CodeEditor::updateBottomMargin()
{
    auto *doc = document();
    if (doc->blockCount() > 1)
    {
        // calling QTextFrame::setFrameFormat with an empty document makes the application crash
        auto *rf = doc->rootFrame();
        auto format = rf->frameFormat();
        int documentMargin = static_cast<int>(doc->documentMargin());
        int bottomMargin = SettingsHelper::isExtraBottomMargin()
                               ? qMax(0, viewport()->height() - fontMetrics().height() - documentMargin)
                               : documentMargin;
        if (format.bottomMargin() != bottomMargin)
        {
            format.setBottomMargin(bottomMargin);
            rf->setFrameFormat(format);
        }
    }
}

void CodeEditor::highlightOccurrences()
{
    occurrencesExtraSelections.clear();

    auto cursor = textCursor();
    if (cursor.hasSelection())
    {
        auto text = cursor.selectedText();
        if (QRegularExpression(
                R"((?:[_a-zA-Z][_a-zA-Z0-9]*)|(?<=\b|\s|^)(?i)(?:(?:(?:(?:(?:\d+(?:'\d+)*)?\.(?:\d+(?:'\d+)*)(?:e[+-]?(?:\d+(?:'\d+)*))?)|(?:(?:\d+(?:'\d+)*)\.(?:e[+-]?(?:\d+(?:'\d+)*))?)|(?:(?:\d+(?:'\d+)*)(?:e[+-]?(?:\d+(?:'\d+)*)))|(?:0x(?:[0-9a-f]+(?:'[0-9a-f]+)*)?\.(?:[0-9a-f]+(?:'[0-9a-f]+)*)(?:p[+-]?(?:\d+(?:'\d+)*)))|(?:0x(?:[0-9a-f]+(?:'[0-9a-f]+)*)\.?(?:p[+-]?(?:\d+(?:'\d+)*))))[lf]?)|(?:(?:(?:[1-9]\d*(?:'\d+)*)|(?:0[0-7]*(?:'[0-7]+)*)|(?:0x[0-9a-f]+(?:'[0-9a-f]+)*)|(?:0b[01]+(?:'[01]+)*))(?:u?l{0,2}|l{0,2}u?)))(?=\b|\s|$))")
                .match(text)
                .captured() == text)
        {
            auto *doc = document();
            cursor = doc->find(text, 0, QTextDocument::FindWholeWords | QTextDocument::FindCaseSensitively);
            while (!cursor.isNull())
            {
                if (cursor != textCursor())
                {
                    QTextEdit::ExtraSelection e;
                    e.cursor = cursor;
                    e.format.setBackground(
                        {highlighter->theme().editorColor(KSyntaxHighlighting::Theme::TextSelection)});
                    occurrencesExtraSelections.push_back(e);
                }
                cursor = doc->find(text, cursor, QTextDocument::FindWholeWords | QTextDocument::FindCaseSensitively);
            }
        }
    }

    updateExtraSelections();
}

void CodeEditor::indent()
{
    addInEachLineOfSelection(QRegularExpression("^"), SettingsHelper::isReplaceTabs() ? m_tabReplace : "\t");
}

void CodeEditor::unindent()
{
    removeInEachLineOfSelection(QRegularExpression(QString("^(\t| {1,%1})").arg(SettingsHelper::getTabWidth())), true);
}

void CodeEditor::swapLineUp()
{
    auto cursor = textCursor();
    auto lines = toPlainText().remove('\r').split('\n');
    int selectionStart = cursor.selectionStart();
    int selectionEnd = cursor.selectionEnd();
    bool cursorAtEnd = cursor.position() == selectionEnd;
    cursor.setPosition(selectionStart);
    int lineStart = cursor.blockNumber();
    cursor.setPosition(selectionEnd);
    int lineEnd = cursor.blockNumber();

    if (lineStart == 0)
        return;
    selectionStart -= lines[lineStart - 1].length() + 1;
    selectionEnd -= lines[lineStart - 1].length() + 1;
    lines.move(lineStart - 1, lineEnd);

    cursor.select(QTextCursor::Document);
    cursor.insertText(lines.join('\n'));

    if (cursorAtEnd)
    {
        cursor.setPosition(selectionStart);
        cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    }
    else
    {
        cursor.setPosition(selectionEnd);
        cursor.setPosition(selectionStart, QTextCursor::KeepAnchor);
    }

    setTextCursor(cursor);
}

void CodeEditor::swapLineDown()
{
    auto cursor = textCursor();
    auto lines = toPlainText().remove('\r').split('\n');
    int selectionStart = cursor.selectionStart();
    int selectionEnd = cursor.selectionEnd();
    bool cursorAtEnd = cursor.position() == selectionEnd;
    cursor.setPosition(selectionStart);
    int lineStart = cursor.blockNumber();
    cursor.setPosition(selectionEnd);
    int lineEnd = cursor.blockNumber();

    if (lineEnd == document()->blockCount() - 1)
        return;
    selectionStart += lines[lineEnd + 1].length() + 1;
    selectionEnd += lines[lineEnd + 1].length() + 1;
    lines.move(lineEnd + 1, lineStart);

    cursor.select(QTextCursor::Document);
    cursor.insertText(lines.join('\n'));

    if (cursorAtEnd)
    {
        cursor.setPosition(selectionStart);
        cursor.setPosition(selectionEnd, QTextCursor::KeepAnchor);
    }
    else
    {
        cursor.setPosition(selectionEnd);
        cursor.setPosition(selectionStart, QTextCursor::KeepAnchor);
    }

    setTextCursor(cursor);
}

void CodeEditor::deleteLine()
{
    auto cursor = textCursor();
    int selectionStart = cursor.selectionStart();
    int selectionEnd = cursor.selectionEnd();
    cursor.setPosition(selectionStart);
    int lineStart = cursor.blockNumber();
    cursor.setPosition(selectionEnd);
    int lineEnd = cursor.blockNumber();
    int columnNumber = textCursor().columnNumber();
    cursor.movePosition(QTextCursor::Start);
    if (lineEnd == document()->blockCount() - 1)
    {
        if (lineStart == 0)
        {
            cursor.select(QTextCursor::Document);
        }
        else
        {
            cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, lineStart - 1);
            cursor.movePosition(QTextCursor::EndOfBlock);
            cursor.movePosition(QTextCursor::End, QTextCursor::KeepAnchor);
        }
    }
    else
    {
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, lineStart);
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, lineEnd - lineStart + 1);
    }
    cursor.removeSelectedText();
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor,
                        qMin(columnNumber, cursor.block().text().length()));
    setTextCursor(cursor);
}

void CodeEditor::duplicate()
{
    auto cursor = textCursor();
    if (cursor.hasSelection()) // duplicate the selection
    {
        auto text = cursor.selectedText();
        bool cursorAtEnd = cursor.selectionEnd() == cursor.position();
        cursor.insertText(text + text);
        if (cursorAtEnd)
        {
            cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::MoveAnchor, text.length());
            cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, text.length());
        }
        else
        {
            cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor, text.length());
        }
    }
    else // duplicate the current line
    {
        int column = cursor.columnNumber();
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        auto text = cursor.selectedText();
        cursor.insertText(text + "\n" + text);
        cursor.movePosition(QTextCursor::StartOfBlock);
        cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, column);
    }
    setTextCursor(cursor);
}

void CodeEditor::toggleComment()
{
    const QString comment = language == "Python" ? "#" : "//";

    if (!removeInEachLineOfSelection(QRegularExpression("^\\s*(" + comment + " ?)"), false))
    {
        addInEachLineOfSelection(QRegularExpression("\\S|^\\s*$"), comment + " ");
    }
}

void CodeEditor::toggleBlockComment()
{
    const QString commentStart = language == "Python" ? "'''" : "/*";
    const QString commentEnd = language == "Python" ? "'''" : "*/";

    if (commentStart.isEmpty() || commentEnd.isEmpty())
        return;

    auto cursor = textCursor();
    int startPos = cursor.selectionStart();
    int endPos = cursor.selectionEnd();
    bool cursorAtEnd = cursor.position() == endPos;
    auto text = cursor.selectedText();
    int pos1 = 0;
    int pos2 = 0;
    if (text.indexOf(commentStart) == 0 && text.length() >= commentStart.length() + commentEnd.length() &&
        text.lastIndexOf(commentEnd) + commentEnd.length() == text.length())
    {
        insertPlainText(text.mid(commentStart.length(), text.length() - commentStart.length() - commentEnd.length()));
        pos1 = startPos;
        pos2 = endPos - commentStart.length() - commentEnd.length();
    }
    else
    {
        insertPlainText(commentStart + text + commentEnd);
        pos1 = startPos;
        pos2 = endPos + commentStart.length() + commentEnd.length();
    }
    if (cursorAtEnd)
    {
        cursor.setPosition(pos1);
        cursor.setPosition(pos2, QTextCursor::KeepAnchor);
    }
    else
    {
        cursor.setPosition(pos2);
        cursor.setPosition(pos1, QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
}

void CodeEditor::highlightParentheses()
{
    parenthesesExtraSelections.clear();

    auto currentSymbol = charUnderCursor();
    auto prevSymbol = charUnderCursor(-1);

    for (auto &p : parentheses)
    {
        int direction = 0;

        QChar counterSymbol;
        QChar activeSymbol;
        auto position = textCursor().position();

        if (p.left == currentSymbol)
        {
            direction = 1;
            counterSymbol = p.right;
            activeSymbol = currentSymbol;
        }
        else if (p.right == prevSymbol)
        {
            direction = -1;
            counterSymbol = p.left;
            activeSymbol = prevSymbol;
            position--;
        }
        else
        {
            continue;
        }

        auto counter = 1;

        while (counter != 0 && position > 0 && position < (document()->characterCount() - 1))
        {
            // Moving position
            position += direction;

            auto character = document()->characterAt(position);
            // Checking symbol under position
            if (character == activeSymbol)
            {
                ++counter;
            }
            else if (character == counterSymbol)
            {
                --counter;
            }
        }

        // Found
        if (counter == 0)
        {
            QTextEdit::ExtraSelection selection;

            auto directionEnum = direction < 0 ? QTextCursor::MoveOperation::Left : QTextCursor::MoveOperation::Right;

            selection.format.setBackground(
                {highlighter->theme().editorColor(KSyntaxHighlighting::Theme::BracketMatching)});
            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            selection.cursor.movePosition(directionEnum, QTextCursor::MoveMode::MoveAnchor,
                                          qAbs(textCursor().position() - position));

            selection.cursor.movePosition(QTextCursor::MoveOperation::Right, QTextCursor::MoveMode::KeepAnchor, 1);

            parenthesesExtraSelections.append(selection);

            selection.cursor = textCursor();
            selection.cursor.clearSelection();
            selection.cursor.movePosition(directionEnum, QTextCursor::MoveMode::KeepAnchor, 1);

            parenthesesExtraSelections.append(selection);
        }

        break;
    }

    updateExtraSelections();
}

void CodeEditor::highlightCurrentLine()
{
    currentLineExtraSelections.clear();

    if (m_highlightingCurrentLine && !isReadOnly())
    {
        QTextEdit::ExtraSelection selection;

        selection.format.setBackground({highlighter->theme().editorColor(KSyntaxHighlighting::Theme::CurrentLine)});
        selection.format.setProperty(QTextFormat::FullWidthSelection, true);
        selection.cursor = textCursor();
        selection.cursor.clearSelection();

        currentLineExtraSelections.append(selection);
    }

    updateExtraSelections();
}

int CodeEditor::getFirstVisibleBlock()
{
    // Detect the first block for which bounding rect - once translated
    // in absolute coordinated - is contained by the editor's text area

    // Costly way of doing but since "blockBoundingGeometry(...)" doesn't
    // exists for "QPlainTextEdit"...

    QTextCursor curs = QTextCursor(document());
    curs.movePosition(QTextCursor::Start);
    for (int i = 0; i < document()->blockCount(); ++i)
    {
        QTextBlock block = curs.block();

        QRect r1 = viewport()->geometry();
        QRect r2 = document()
                       ->documentLayout()
                       ->blockBoundingRect(block)
                       .translated(viewport()->geometry().x(),
                                   viewport()->geometry().y() - verticalScrollBar()->sliderPosition())
                       .toRect();

        if (r1.intersects(r2))
        {
            return i;
        }

        curs.movePosition(QTextCursor::NextBlock);
    }

    return 0;
}

void CodeEditor::keyPressEvent(QKeyEvent *e)
{
    /* if(m_vimCursor) */
    /* { */
    /*     QPlainTextEdit::keyPressEvent(e); */
    /*     proceedCompleterEnd(); */
    /*     return; */
    /* } */
    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && e->modifiers() != Qt::NoModifier)
    {
        QKeyEvent pureEnter(QEvent::KeyPress, Qt::Key_Enter, Qt::NoModifier);
        if (e->modifiers() == Qt::ControlModifier)
        {
            moveCursor(QTextCursor::EndOfBlock);
            keyPressEvent(&pureEnter);
            return;
        }

        if (e->modifiers() == (Qt::ControlModifier | Qt::ShiftModifier))
        {
            if (textCursor().blockNumber() == 0)
            {
                moveCursor(QTextCursor::StartOfBlock);
                insertPlainText("\n");
                moveCursor(QTextCursor::PreviousBlock);
                moveCursor(QTextCursor::EndOfBlock);
            }
            else
            {
                moveCursor(QTextCursor::PreviousBlock);
                moveCursor(QTextCursor::EndOfBlock);
                keyPressEvent(&pureEnter);
            }
            return;
        }
        if (e->modifiers() == Qt::ShiftModifier)
        {
            keyPressEvent(&pureEnter);
            return;
        }
    }

    if (e->key() == Qt::Key_Tab && e->modifiers() == Qt::NoModifier)
    {
        if (textCursor().hasSelection())
        {
            indent();
            return;
        }

        auto c = charUnderCursor();
        for (auto p : parentheses)
        {
            if (p.tabJumpOut && c == p.right)
            {
                moveCursor(QTextCursor::NextCharacter);
                return;
            }
        }

        if (SettingsHelper::isReplaceTabs())
        {
            insertPlainText(m_tabReplace);
            return;
        }
    }

    if (e->key() == Qt::Key_Backtab && e->modifiers() == Qt::ShiftModifier)
    {
        unindent();
        return;
    }

    if (e->key() == Qt::Key_Delete && e->modifiers() == Qt::ShiftModifier)
    {
        deleteLine();
        return;
    }

    // Auto indentation

    QString indentationSpaces =
        QRegularExpression("^\\s*").match(document()->findBlockByNumber(textCursor().blockNumber()).text()).captured();

    // Have Qt Edior like behaviour, if {|} and enter is pressed indent the two
    // parenthesis
    if (SettingsHelper::isAutoIndent() && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
        e->modifiers() == Qt::NoModifier && charUnderCursor(-1) == '{' && charUnderCursor() == '}')
    {
        insertPlainText("\n" + indentationSpaces + (SettingsHelper::isReplaceTabs() ? m_tabReplace : "\t") + "\n" +
                        indentationSpaces);

        for (int i = 0; i <= indentationSpaces.length(); ++i)
            moveCursor(QTextCursor::MoveOperation::Left);

        return;
    }

    // Auto-indent for single "{" without "}"
    if (SettingsHelper::isAutoIndent() && (e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) &&
        e->modifiers() == Qt::NoModifier && charUnderCursor(-1) == '{')
    {
        insertPlainText("\n" + indentationSpaces + (SettingsHelper::isReplaceTabs() ? m_tabReplace : "\t"));
        setTextCursor(textCursor()); // scroll to the cursor
        return;
    }

    // Toggle Overwrite and insert mode in non-vim modes.
    if (e->key() == Qt::Key_Insert && !m_vimCursor)
    {
        setOverwriteMode(!overwriteMode());
        if (overwriteMode())
        {
            QFontMetrics fm(QPlainTextEdit::font());
            const int position = QPlainTextEdit::textCursor().position();
            const QChar c = QPlainTextEdit::document()->characterAt(position);
            setCursorWidth(fm.horizontalAdvance(c));
        }
        else
        {
            auto rect = cursorRect();
            setCursorWidth(1);
            viewport()->update(rect);
        }

        return;
    }

    if (e->key() == Qt::Key_Backspace && e->modifiers() == Qt::NoModifier && !textCursor().hasSelection())
    {
        auto pre = charUnderCursor(-1);
        auto nxt = charUnderCursor();
        for (auto p : parentheses)
        {
            if (p.autoRemove && p.left == pre && p.right == nxt)
            {
                auto cursor = textCursor();
                cursor.movePosition(QTextCursor::Left, QTextCursor::MoveAnchor);
                cursor.movePosition(QTextCursor::Right, QTextCursor::KeepAnchor, 2);
                cursor.removeSelectedText();
                setTextCursor(textCursor()); // scroll to the cursor
                return;
            }
        }

        if (textCursor().columnNumber() <= indentationSpaces.length() && textCursor().columnNumber() >= 1 &&
            !m_tabReplace.isEmpty())
        {
            auto cursor = textCursor();
            int realColumn = 0;
            int newIndentLength = 0;
            for (int i = 0; i < cursor.columnNumber(); ++i)
            {
                if (indentationSpaces[i] != '\t')
                    ++realColumn;
                else
                {
                    realColumn = (realColumn + m_tabReplace.length()) / m_tabReplace.length() * m_tabReplace.length();
                }
                if (realColumn % m_tabReplace.length() == 0 && i < cursor.columnNumber() - 1)
                {
                    newIndentLength = i + 1;
                }
            }
            cursor.movePosition(QTextCursor::PreviousCharacter, QTextCursor::KeepAnchor,
                                cursor.columnNumber() - newIndentLength);
            cursor.removeSelectedText();
            setTextCursor(textCursor()); // scroll to the cursor
            return;
        }
    }

    for (auto p : parentheses)
    {
        if (p.autoComplete)
        {
            auto cursor = textCursor();
            if (cursor.hasSelection())
            {
                if (p.left == e->text())
                {
                    // Add parentheses for selection
                    int startPos = cursor.selectionStart();
                    int endPos = cursor.selectionEnd();
                    bool cursorAtEnd = cursor.position() == endPos;
                    auto text = p.left + cursor.selectedText() + p.right;
                    insertPlainText(text);
                    if (cursorAtEnd)
                    {
                        cursor.setPosition(startPos + 1);
                        cursor.setPosition(endPos + 1, QTextCursor::KeepAnchor);
                    }
                    else
                    {
                        cursor.setPosition(endPos + 1);
                        cursor.setPosition(startPos + 1, QTextCursor::KeepAnchor);
                    }
                    setTextCursor(cursor);
                    return;
                }
            }
            else
            {
                if (p.right == e->text())
                {
                    auto symbol = charUnderCursor();

                    if (symbol == p.right)
                    {
                        moveCursor(QTextCursor::NextCharacter);
                        return;
                    }
                }

                if (p.left == e->text())
                {
                    insertPlainText(QString(p.left) + p.right);
                    moveCursor(QTextCursor::PreviousCharacter);
                    return;
                }
            }
        }
    }

    if ((e->key() == Qt::Key_Return || e->key() == Qt::Key_Enter) && e->modifiers() == Qt::NoModifier)
    {
        insertPlainText("\n" + indentationSpaces.left(textCursor().columnNumber()));
        setTextCursor(textCursor()); // scroll to the cursor
        return;
    }

    QPlainTextEdit::keyPressEvent(e);
}

bool CodeEditor::event(QEvent *event)
{
    if (event->type() == QEvent::ToolTip)
    {
        auto *helpEvent = dynamic_cast<QHelpEvent *>(event);
        auto point = helpEvent->pos();
        QTextCursor cursor = cursorForPosition(point);

        auto lineNumber = cursor.blockNumber() + 1;

        QTextCursor copyCursor(cursor);
        copyCursor.movePosition(QTextCursor::StartOfBlock);

        auto blockPositionStart = cursor.positionInBlock() - copyCursor.positionInBlock();
        QPair<int, int> positionOfTooltip{lineNumber, blockPositionStart};

        QString text;
        for (auto const &e : squiggles)
        {
            if (e.m_startPos <= positionOfTooltip && e.m_stopPos >= positionOfTooltip)
            {
                if (text.isEmpty())
                    text = e.m_tooltipText;
                else
                    text += "; " + e.m_tooltipText;
            }
        }

        if (text.isEmpty())
            QToolTip::hideText();
        else
            QToolTip::showText(helpEvent->globalPos(), text);

        return true;
    }
    return QPlainTextEdit::event(event);
}

void CodeEditor::squiggle(SeverityLevel level, QPair<int, int> start, QPair<int, int> stop, QString tooltipMessage)
{
    if (stop < start)
        return;

    SquiggleInformation info(start, stop, std::move(tooltipMessage));
    squiggles.push_back(info);

    auto cursor = textCursor();

    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, start.first - 1);
    cursor.movePosition(QTextCursor::StartOfBlock);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::MoveAnchor, start.second);

    if (stop.first > start.first)
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, stop.first - start.first);

    cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, stop.second);

    QTextCharFormat newcharfmt = currentCharFormat();
    newcharfmt.setFontUnderline(true);

    switch (level)
    {
    case SeverityLevel::Error:
        newcharfmt.setUnderlineColor(highlighter->theme().textColor(KSyntaxHighlighting::Theme::Error));
        // newcharfmt.setUnderlineStyle(m_syntaxStyle->getFormat("Error").underlineStyle());
        break;
    case SeverityLevel::Warning:
        newcharfmt.setUnderlineColor(highlighter->theme().textColor(KSyntaxHighlighting::Theme::Warning));
        // newcharfmt.setUnderlineStyle(m_syntaxStyle->getFormat("Warning").underlineStyle());
        break;
    case SeverityLevel::Information:
        newcharfmt.setUnderlineColor(highlighter->theme().textColor(KSyntaxHighlighting::Theme::Information));
        newcharfmt.setUnderlineStyle(QTextCharFormat::DotLine);
        break;
    case SeverityLevel::Hint:
        newcharfmt.setUnderlineColor(highlighter->theme().textColor(KSyntaxHighlighting::Theme::Alert));
        newcharfmt.setUnderlineStyle(QTextCharFormat::DotLine);
    }

    squigglesExtraSelections.push_back({cursor, newcharfmt});

    updateExtraSelections();
}

void CodeEditor::clearSquiggle()
{
    if (squiggles.empty())
        return;

    squiggles.clear();
    squigglesExtraSelections.clear();

    updateExtraSelections();
}

QChar CodeEditor::charUnderCursor(int offset) const
{
    auto block = textCursor().blockNumber();
    auto index = textCursor().positionInBlock();
    auto text = document()->findBlockByNumber(block).text();

    index += offset;

    if (index < 0 || index >= text.size())
    {
        return {};
    }

    return text[index];
}

bool CodeEditor::isPositionInsideBlockComments(int position) const
{
    return true;
}

bool CodeEditor::isPositionInsideLineComments(int position) const
{
    return true;
}

bool CodeEditor::isPositionInsideSingleQuotes(int position) const
{
    return true;
}

bool CodeEditor::isPositionInsideDoubleQuotes(int position) const
{
    return true;
}

bool CodeEditor::isPositionPartOfRawOrStringLiteral(int position) const
{
    return true;
}

void CodeEditor::insertFromMimeData(const QMimeData *source)
{
    insertPlainText(source->text());
}

bool CodeEditor::removeInEachLineOfSelection(const QRegularExpression &regex, bool force)
{
    auto cursor = textCursor();
    auto lines = toPlainText().remove('\r').split('\n');
    int selectionStart = cursor.selectionStart();
    int selectionEnd = cursor.selectionEnd();
    bool cursorAtEnd = cursor.position() == selectionEnd;
    cursor.setPosition(selectionStart);
    int lineStart = cursor.blockNumber();
    cursor.setPosition(selectionEnd);
    int lineEnd = cursor.blockNumber();
    QString newText;
    QTextStream stream(&newText);
    int deleteTotal = 0;
    int deleteFirst = 0;
    for (int i = lineStart; i <= lineEnd; ++i)
    {
        auto line = lines[i];
        auto match = regex.match(line).captured(1);
        int len = match.length();
        if (len == 0 && !force)
            return false;
        if (i == lineStart)
            deleteFirst = len;
        deleteTotal += len;
        stream << line.remove(line.indexOf(match), len);
        if (i != lineEnd)
            stream << Qt::endl;
    }
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, lineStart);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, lineEnd - lineStart);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(newText);
    cursor.setPosition(qMax(0, selectionStart - deleteFirst));
    if (cursor.blockNumber() < lineStart)
    {
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, lineStart - cursor.blockNumber());
        cursor.movePosition(QTextCursor::StartOfBlock);
    }
    int pos = cursor.position();
    cursor.setPosition(selectionEnd - deleteTotal);
    if (cursor.blockNumber() < lineEnd)
    {
        cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, lineEnd - cursor.blockNumber());
        cursor.movePosition(QTextCursor::StartOfBlock);
    }
    int pos2 = cursor.position();
    if (cursorAtEnd)
    {
        cursor.setPosition(pos);
        cursor.setPosition(pos2, QTextCursor::KeepAnchor);
    }
    else
    {
        cursor.setPosition(pos2);
        cursor.setPosition(pos, QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
    return true;
}

void CodeEditor::addInEachLineOfSelection(const QRegularExpression &regex, const QString &str)
{
    auto cursor = textCursor();
    auto lines = toPlainText().remove('\r').split('\n');
    int selectionStart = cursor.selectionStart();
    int selectionEnd = cursor.selectionEnd();
    bool cursorAtEnd = cursor.position() == selectionEnd;
    cursor.setPosition(selectionStart);
    int lineStart = cursor.blockNumber();
    cursor.setPosition(selectionEnd);
    int lineEnd = cursor.blockNumber();
    QString newText;
    QTextStream stream(&newText);
    for (int i = lineStart; i <= lineEnd; ++i)
    {
        auto line = lines[i];
        stream << line.insert(line.indexOf(regex), str);
        if (i != lineEnd)
            stream << Qt::endl;
    }
    cursor.movePosition(QTextCursor::Start);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::MoveAnchor, lineStart);
    cursor.movePosition(QTextCursor::NextBlock, QTextCursor::KeepAnchor, lineEnd - lineStart);
    cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    cursor.insertText(newText);
    int pos = selectionStart + str.length();
    int pos2 = selectionEnd + str.length() * (lineEnd - lineStart + 1);
    if (cursorAtEnd)
    {
        cursor.setPosition(pos);
        cursor.setPosition(pos2, QTextCursor::KeepAnchor);
    }
    else
    {
        cursor.setPosition(pos2);
        cursor.setPosition(pos, QTextCursor::KeepAnchor);
    }
    setTextCursor(cursor);
}

void CodeEditor::updateExtraSelections()
{
    QPlainTextEdit::setExtraSelections(currentLineExtraSelections + parenthesesExtraSelections +
                                       occurrencesExtraSelections + squigglesExtraSelections);
}
} // namespace Editor
