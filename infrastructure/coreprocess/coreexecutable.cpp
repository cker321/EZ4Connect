#include "coreexecutable.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QProcess>
#include <QSysInfo>

#include <stdexcept>

QString CoreExecutable::path()
{
    // 本项目锁定使用旧版内核（cker321/zju-connect），内核二进制固定命名为
    // zju-connect-legacy，以区分新版 zju-connect。只从应用同目录查找该固定
    // 文件名，不再 fallback 到 PATH，避免误用新版内核导致参数不兼容。
    const QString fileName =
        QSysInfo::productType() == "windows" ? "zju-connect-legacy.exe" : "zju-connect-legacy";
    const QString bundledPath = QCoreApplication::applicationDirPath() + "/" + fileName;
    if (!QFileInfo::exists(bundledPath))
    {
        throw std::runtime_error(
            "缺少旧版内核二进制 zju-connect-legacy，请将其放置到应用目录"
        );
    }
    return bundledPath;
}

QString CoreExecutable::version(QObject *parent)
{
    QProcess process(parent);
    process.start(path(), {"-version"});
    if (!process.waitForStarted())
    {
        throw std::runtime_error("核心无法启动");
    }
    if (!process.waitForFinished())
    {
        throw std::runtime_error("核心运行超时");
    }

    const QByteArray errorOutput = process.readAllStandardError();
    if (!errorOutput.isEmpty())
    {
        throw std::runtime_error(errorOutput);
    }

    const QString output = process.readAllStandardOutput();
    const QString prefix("ZJU Connect v");
    if (!output.startsWith(prefix))
    {
        throw std::runtime_error("无法解析核心版本号");
    }
    return output.mid(prefix.size()).trimmed();
}
