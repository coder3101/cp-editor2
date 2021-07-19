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

/* Most of the code in this file is under the following copyright
    Copyright (c) 2017, Lukas Holecek <hluk@email.cz>
    This file is part of CopyQ.
    CopyQ is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    CopyQ is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with CopyQ.  If not, see <http://www.gnu.org/licenses/>.
*/

#ifndef FAKE_VIM_PROXY_HPP
#define FAKE_VIM_PROXY_HPP

#include "Core/FakeVimCommand.hpp"
#include "third_party/FakeVim/fakevim/fakevimhandler.h"
#include <QObject>

class AppWindow;
class MainWindow;
class QLabel;

namespace Core
{
class FakeVimProxy : public QObject
{
    Q_OBJECT

  public:
    FakeVimProxy(QWidget *widget, MainWindow *mw, AppWindow *aw, QObject *parent = nullptr);
    ~FakeVimProxy() override;
    static void sourceVimRc(FakeVim::Internal::FakeVimHandler *handler);
    static void initHandler(FakeVim::Internal::FakeVimHandler *handler);
    static void clearUndoRedo(QWidget *);
    static void connectSignals(FakeVim::Internal::FakeVimHandler *handler, QWidget *editor, MainWindow *mainwindow,
                               AppWindow *appwin);

  signals:
    void handleInput(QString const &key);

  public slots:
    void changeStatusData(QString const &);
    void highlightMatches(QString const &);
    void changeStatusMessage(QString const &, int);
    void handleExCommand(bool *, const FakeVim::Internal::ExCommand &);
    void requestSetBlockSelection(QTextCursor const &);
    void requestDisableBlockSelection();
    void updateBlockSelection();
    void requestHasBlockSelection(bool *);
    void indentRegion(int, int, QChar);
    void moveToMatchingParenthesis(bool *, bool *, QTextCursor *);
    static void checkForElectricCharacter(bool *, QChar);

  private:
    static int firstNonSpace(QString const &);
    QChar charUnderCursor(QTextCursor *, int);
    QChar getCounterParenthesis(QChar);
    void updateExtraSelections();
    static bool wantSaveAndQuit(FakeVim::Internal::ExCommand const &);
    static bool wantSave(FakeVim::Internal::ExCommand const &);
    static bool wantQuit(FakeVim::Internal::ExCommand const &);
    bool save();
    void quit();
    void forceQuit();
    void setStatusBar();
    bool hasChanges();
    QTextDocument *document() const;
    QString content() const;

    QWidget *m_widget;
    MainWindow *m_mainWindow;
    AppWindow *m_appWindow;
    FakeVimCommand *m_commandHandler;
    QLabel *m_statusMessage = nullptr;
    QLabel *m_statusData = nullptr;

    QVector<QPair<QChar, QChar>> parenthesisList = {{'{', '}'}, {'(', ')'}, {'[', ']'}};
    QList<QTextEdit::ExtraSelection> m_searchSelection;
    QList<QTextEdit::ExtraSelection> m_clearSelection;
    QList<QTextEdit::ExtraSelection> m_blockSelection;
};
} // namespace Core

#endif // FAKE_VIM_PROXY_HPP
