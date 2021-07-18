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

#include "Core/EventLogger.hpp"
#include "Util/FileUtil.hpp"
#include "generated/portable.hpp"
#include "generated/version.hpp"
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QLibraryInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QSysInfo>
#include <QUrl>

namespace Core
{

QFile Log::logFile;
QTextStream Log::logStream;

const int Log::NUMBER_OF_LOGS_TO_KEEP = 50;
const int Log::MAXIMUM_FUNCTION_NAME_SIZE = 30;
const int Log::MAXIMUM_FILE_NAME_SIZE = 30;
const QString Log::LOG_DIR_NAME = "log";
const QString Log::LOG_FILE_NAME = "cpeditor";

void Log::init(unsigned int instance, bool dumptoStderr)
{
    logStream.setDevice(&logFile);
    if (!dumptoStderr)
    {
        // get the path to the log file
        auto path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
        LOG_ERR_IF(path.isEmpty(), "Failed to get writable temp location");

        QDir dir(path);
        dir.mkpath(path);
        dir.mkdir(LOG_DIR_NAME);

        if (dir.cd(LOG_DIR_NAME))
        {
            // keep NUMBER_OF_LOGS_TO_KEEP log files
            auto entries = dir.entryList({LOG_FILE_NAME + "*.log"}, QDir::Files, QDir::Time);
            for (int i = NUMBER_OF_LOGS_TO_KEEP; i < entries.length(); ++i)
                dir.remove(entries[i]);

            // open the log file
            logFile.setFileName(dir.filePath(QString("%1-%2-%3.log")
                                                 .arg(LOG_FILE_NAME)
                                                 .arg(QDateTime::currentDateTime().toString("yyyy-MM-dd-hh-mm-ss-zzz"))
                                                 .arg(instance)));
            logFile.open(QIODevice::WriteOnly | QFile::Text);
            LOG_ERR_IF(!logFile.isOpen() || !logFile.isWritable(), "Failed to open file" << logFile.fileName());
        }
        else
        {
            LOG_ERR("Failed to open directory" << dir.filePath(LOG_DIR_NAME));
        }
    }
    else
    {
        logFile.open(stderr, QIODevice::WriteOnly);
    }
    LOG_INFO("Event logger has been initialized successfully");
    platformInformation();
}

QString Log::dateTimeStamp()
{
    return "[" + QDateTime::currentDateTime().toString(Qt::ISODateWithMs) + "]";
}

void Log::platformInformation()
{
    LOG_INFO("Gathering system information");
    // Check https://doc.qt.io/qt-5/qsysinfo.html to know what each identifier means
    LOG_INFO(INFO_OF(QSysInfo::buildAbi()));
    LOG_INFO(INFO_OF(QSysInfo::buildCpuArchitecture()));
    LOG_INFO(INFO_OF(QSysInfo::currentCpuArchitecture()));
    LOG_INFO(INFO_OF(QSysInfo::kernelType()));
    LOG_INFO(INFO_OF(QSysInfo::kernelVersion()));
    LOG_INFO(INFO_OF(QSysInfo::prettyProductName()));
    LOG_INFO(INFO_OF(QSysInfo::productType()));
    LOG_INFO(INFO_OF(QSysInfo::productVersion()));
    LOG_INFO(INFO_OF(QLibraryInfo::version().toString()));

    LOG_INFO(INFO_OF(APP_VERSION));
    LOG_INFO(INFO_OF(DISPLAY_VERSION));
    LOG_INFO(INFO_OF(GIT_COMMIT_HASH));
#ifdef PORTABLE_VERSION
    LOG_INFO("Portable Version");
#else
    LOG_INFO("Setup Version");
#endif
#ifdef QT_DEBUG
    LOG_INFO("Debug Build");
#else
    LOG_INFO("Release Build");
#endif
    LOG_INFO(INFO_OF(__DATE__));
    LOG_INFO(INFO_OF(__TIME__));
}

QTextStream &Log::log(const QString &priority, QString funcName, int line, QString fileName)
{
    if (!logFile.isOpen() || !logFile.isWritable())
        logFile.open(stderr, QIODevice::WriteOnly); // dump to stderr if failed to open log file
    if (funcName.size() > MAXIMUM_FUNCTION_NAME_SIZE)
        funcName = funcName.right(MAXIMUM_FUNCTION_NAME_SIZE);

    QFileInfo info(fileName);
    fileName = info.fileName();

    if (fileName.size() > MAXIMUM_FILE_NAME_SIZE)
        fileName = fileName.right(MAXIMUM_FILE_NAME_SIZE);

    return logStream << dateTimeStamp() << Qt::center << "[" << priority << "]["
                     << qSetFieldWidth(MAXIMUM_FUNCTION_NAME_SIZE) << funcName << qSetFieldWidth(0) << "]["
                     << qSetFieldWidth(MAXIMUM_FILE_NAME_SIZE) << fileName << qSetFieldWidth(0) << Qt::left << "]"
                     << "(" << line << ")::";
}

void Log::revealInFileManager()
{
    Util::revealInFileManager(logFile.fileName()).first();
}

void Log::clearOldLogs()
{
    auto path = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    QDir dir(path);
    if (dir.cd(LOG_DIR_NAME))
    {
        auto entries = dir.entryList({LOG_FILE_NAME + "*.log"}, QDir::Files);
        for (auto const &e : entries)
        {
            if (e != logFile.fileName()) // clear all except the current
            {
                dir.remove(e);
                LOG_INFO("Deleted log file:" << e);
            }
        }
    }
}

} // namespace Core
