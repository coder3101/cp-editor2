/*
 * Copyright (C) 2019-2021 Ashar Khan <ashar786khan@gmail.com>
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

#ifndef LANGUAGEREPOSITORY_HPP
#define LANGUAGEREPOSITORY_HPP

#include <QObject>

class QRegularExpression;

namespace Editor
{
class LanguageRepository : public QObject
{
  public:
    LanguageRepository(QString language, QObject *parent = nullptr);
    void changeLanguage(QString newLang);

    QString singleQuoteToken() const;
    QString doubleQuoteToken() const;
    QString singleLineCommentToken() const;
    QPair<QString, QString> blockCommentTokens() const;
    QPair<QString, QString> rawStringTokens() const;

    QRegularExpression singleQuoteRegEx() const;
    QRegularExpression doubleQuoteRegEx() const;
    QRegularExpression singleLineCommentRegEx() const;
    QPair<QRegularExpression, QRegularExpression> blockCommentRegExes() const;
    QPair<QRegularExpression, QRegularExpression> rawStringRegExes() const;

  private:
    QString language;
};

} // namespace Editor
#endif
