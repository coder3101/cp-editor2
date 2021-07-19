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

#include "Editor/KSHRepository.hpp"
#include <QStringList>
#include <theme.h>

namespace Editor
{
KSyntaxHighlighting::Repository KSyntaxHighlightingRepository::repository;

KSyntaxHighlighting::Repository *KSyntaxHighlightingRepository::getSyntaxHighlightingRepository()
{
    return &repository;
}

QStringList KSyntaxHighlightingRepository::themeNames()
{
    QStringList names;
    for (const auto &theme : repository.themes())
        names.push_back(theme.name());
    return names;
}
} // namespace Editor
