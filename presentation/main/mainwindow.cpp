#include <QMessageBox>
#include <QSysInfo>
#include <QNetworkInterface>
#include <QClipboard>
#include <QDesktopServices>
#include <QDebug>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QActionGroup>
#include <QInputDialog>
#include <QMargins>
#include <QStyle>
#include <QStyleHints>
#include <QTimer>
#include <QLabel>
#include <QPushButton>
#include <QTabWidget>
#include <QVBoxLayout>

#include "mainwindow.h"

#include "application/applicationlogger.h"
#include "application/applicationconstants.h"
#include "application/commandlineoptions.h"
#include "application/settingsmigrator.h"
#include "infrastructure/coreprocess/devicetrust.h"
#include "infrastructure/logging/applicationlogfile.h"
#include "infrastructure/storage/applicationpaths.h"
#include "infrastructure/update/updatechecker.h"
#include "presentation/coordinators/connectionuicontroller.h"
#include "presentation/dialogs/configurationguidedialog/configurationguidedialog.h"
#include "presentation/presentationhelpers.h"
#include "ui_mainwindow.h"

namespace
{
bool appendStyleSheet(const QString &path, QString &styleSheet)
{
    QFile styleFile(path);
    if (!styleFile.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        qWarning().noquote() << "无法加载样式资源：" << path;
        return false;
    }

    styleSheet.append(QString::fromUtf8(styleFile.readAll()));
    styleSheet.append('\n');
    return true;
}
}

MainWindow::MainWindow(
    ApplicationLogger *logger,
    ApplicationLogFile *logFile,
    QWidget *parent
) :
    QMainWindow(parent),
    ui(new Ui::MainWindow),
    applicationLogger(logger),
    applicationLogFile(logFile)
{
    const QString overrideConfigPath =
        CommandLineOptions::value(QCoreApplication::arguments(), "--config-path");
    coordinator = new MainWindowCoordinator(this, overrideConfigPath, this);
    profileService = coordinator->profiles();
    sessionManager = coordinator->sessions();
    settings = profileService->settings();

    const bool isFirstLaunch =
        !settings->contains("Common/ConfigVersion");
    upgradeSettings();

    ui->setupUi(this);
#ifdef Q_OS_MACOS
    const QMargins centralMargins = ui->centralWidget->contentsMargins();
    ui->centralWidget->setContentsMargins(centralMargins.left(), 12, centralMargins.right(), centralMargins.bottom());
#endif
    ui->logPlainTextEdit->setFont(
        QFontDatabase::systemFont(QFontDatabase::FixedFont)
    );
    QStyleHints *styleHints = QGuiApplication::styleHints();
    applyColorScheme(styleHints->colorScheme());
    connect(styleHints, &QStyleHints::colorSchemeChanged,
            this, &MainWindow::applyColorScheme);
    authenticationDialogs = coordinator->authenticationDialogs();
    systemProxySession = coordinator->systemProxy();
    updateChecker = coordinator->updates();
    connect(updateChecker, &UpdateChecker::versionInfoChanged, this,
            [this](const VersionInfo &) { updateVersionInfo(); });
    connect(updateChecker, &UpdateChecker::checkFailed, this,
            [this](UpdateComponent component, const QString &)
            {
                const QString componentName =
                    component == UpdateComponent::Ui ? QStringLiteral("UI") : QStringLiteral("核心");
                ui->versionLabel->setToolTip(
                    ui->versionLabel->toolTip()
                    + "\n检查" + componentName + "更新失败"
                );
            });
    connect(updateChecker, &UpdateChecker::uiUpdateAvailable, this,
            [this](const QString &latestVersion)
            {
                QMessageBox msgBox(this);
                msgBox.setText("UI 版本更新");
                msgBox.setInformativeText(
                    "存在 UI 版本更新：" + latestVersion + "\n是否前往 Github 发布页面查看？"
                );
                msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
                msgBox.setDefaultButton(QMessageBox::Ok);

                if (msgBox.exec() == QMessageBox::Ok)
                {
                    QDesktopServices::openUrl(
                        QUrl(
                            "https://github.com/" +
                            ApplicationConstants::RepositoryName +
                            "/releases/latest"
                        )
                    );
                }
            });
    connect(applicationLogger, &ApplicationLogger::entryAdded, this,
            [this](const QString &entry)
            {
                ui->logPlainTextEdit->appendPlainText(entry);
            });
    connect(systemProxySession, &SystemProxySession::busyChanged, this,
            [this](bool busy)
            {
                const QString profileId = activeTabProfileId();
                if (!tabWidgets.contains(profileId))
                {
                    return;
                }
                tabWidgets[profileId].proxyButton->setEnabled(!busy);
                ui->disableProxyAction->setEnabled(!busy);
            });
    connect(systemProxySession, &SystemProxySession::enabledChanged, this,
            [this](bool enabled)
            {
                const QString profileId = activeTabProfileId();
                if (tabWidgets.contains(profileId))
                {
                    tabWidgets[profileId].proxyButton->setText(
                        enabled ? "清除系统代理" : "设置系统代理"
                    );
                    if (!enabled && !sessionManager->anyActive())
                    {
                        tabWidgets[profileId].proxyButton->hide();
                    }
                    ConnectionSession *session = sessionManager->find(profileId);
                    if (session != nullptr)
                    {
                        updateTabConnectionState(profileId, session->state());
                    }
                }
            });
    setupTrayIcon();
    setupProfileMenu();
    setupConnectionTabs();

    setWindowIcon(QIcon(QPixmap(":/resource/icon.png").scaled(
        512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation
    )));

    ui->applicationNameLabel->setText(QApplication::applicationDisplayName());

    updateVersionInfo();

    // 文件-退出
    connect(ui->exitAction, &QAction::triggered, this, &MainWindow::gracefullyQuit);

    // 文件-设置
    connect(ui->settingAction, &QAction::triggered, this,
            [&]()
            {
                const QString profileId = activeTabProfileId();
                settingWindow = new SettingWindow(this, settings, profileId);
                settingWindow->show();
            });

    connect(ui->settingPushButton, &QPushButton::clicked,
            ui->settingAction, &QAction::trigger);
    connect(ui->guidePushButton, &QPushButton::clicked,
            ui->configurationGuideAction, &QAction::trigger);
    connect(ui->openLogPushButton, &QPushButton::clicked,
            ui->openLogAction, &QAction::trigger);

    // 文件-打开日志文件
    connect(ui->openLogAction, &QAction::triggered, this,
            [this]()
            {
                const QString logFilePath = applicationLogFile->filePath();
                QFileInfo logFileInfo(logFilePath);

                if (logFileInfo.exists())
                {
                    QDesktopServices::openUrl(QUrl::fromLocalFile(logFilePath));
                }
                else
                {
                    QMessageBox::warning(this, "日志文件", "日志文件创建失败。");
                }
            });

    // 文件-清除系统代理
    connect(ui->disableProxyAction, &QAction::triggered,
            [&]()
            {
                QMessageBox messageBox(this);
                messageBox.setWindowTitle("清理系统代理");
                messageBox.setText("是否清理系统代理？");

                messageBox.addButton(QMessageBox::Yes)->setText("是");
                messageBox.addButton(QMessageBox::No)->setText("否");
                messageBox.setDefaultButton(QMessageBox::Yes);

                if (messageBox.exec() == QMessageBox::No)
                {
                    return;
                }

                connect(systemProxySession, &SystemProxySession::operationFinished, this,
                        [this](bool enabled)
                        {
                            if (!enabled)
                            {
                                qInfo().noquote() << "已清理系统代理设置";
                            }
                        },
                        Qt::SingleShotConnection);
                systemProxySession->disable();
            });

    // 文件-清理登录数据
    connect(ui->clearClientDataAction, &QAction::triggered, this,
            [&]()
            {
                QMessageBox messageBox(this);
                messageBox.setWindowTitle("清理登录缓存");
                messageBox.setText("是否清理登录缓存？");

                messageBox.addButton(QMessageBox::Yes)->setText("是");
                messageBox.addButton(QMessageBox::No)->setText("否");
                messageBox.setDefaultButton(QMessageBox::Yes);

                if (messageBox.exec() == QMessageBox::No)
                {
                    return;
                }

                const QString profileId = activeTabProfileId();
                ApplicationPaths::clearClientData(profileId);
                qInfo().noquote() << "已清理登录缓存";
            });

    // 文件-设置授信设备
    connect(ui->trustDeviceAction, &QAction::triggered, this,
            [&]()
            {
                const QString profileId = activeTabProfileId();
                QSettings *profileSettings = settingsForProfile(profileId);
                try
                {
                    DeviceTrust::set(this,
                        profileSettings->value("ZJUConnect/Protocol", "easyconnect").toString(),
                        profileSettings->value("ZJUConnect/ServerAddress").toString(),
                        profileSettings->value("ZJUConnect/ServerPort").toInt(),
                        profileId, true);
                    qInfo().noquote() << "设置授信设备成功";
                    QMessageBox::information(this, "成功", "已设置授信设备");
                }
                catch (const std::runtime_error &e)
                {
                    qWarning().noquote() << "设置授信设备失败：" + QString(e.what());
                    QMessageBox::critical(this, "错误", "设置授信设备失败：\n" + QString(e.what()));
                }
            });

    // 文件-取消授信设备
    connect(ui->untrustDeviceAction, &QAction::triggered, this,
            [&]()
            {
                const QString profileId = activeTabProfileId();
                QSettings *profileSettings = settingsForProfile(profileId);
                try
                {
                    DeviceTrust::set(this,
                        profileSettings->value("ZJUConnect/Protocol", "easyconnect").toString(),
                        profileSettings->value("ZJUConnect/ServerAddress").toString(),
                        profileSettings->value("ZJUConnect/ServerPort").toInt(),
                        profileId, false);
                    qInfo().noquote() << "取消授信设备成功";
                    QMessageBox::information(this, "成功", "已取消授信设备");
                }
                catch (const std::runtime_error &e)
                {
                    qWarning().noquote() << "取消授信设备失败：" + QString(e.what());
                    QMessageBox::critical(this, "错误", "取消授信设备失败：\n" + QString(e.what()));
                }
            });

    // 帮助-检查更新
    connect(ui->checkUpdateAction, &QAction::triggered,
            updateChecker, &UpdateChecker::check);

    // 帮助-项目主页
    connect(ui->projectAction, &QAction::triggered,
            [&]()
            {
                QDesktopServices::openUrl(QUrl(
                    "https://github.com/" + ApplicationConstants::RepositoryName
                ));
            });

    // 帮助-关于本软件
    connect(ui->aboutAction, &QAction::triggered,
            [&]()
            {
                PresentationHelpers::showAboutDialog(this);
            });

    // 复制日志
    connect(ui->copyLogPushButton, &QPushButton::clicked,
            [&]()
            {
                auto logText = ui->logPlainTextEdit->toPlainText();
                QApplication::clipboard()->setText(logText);
            }
    );

    // 清空日志
    connect(ui->clearLogPushButton, &QPushButton::clicked,
            [&]()
            {
                ui->logPlainTextEdit->clear();
            }
    );

    clearLog();

    bool shouldConnect =
        settings->value("Common/ConnectAfterStart", false).toBool();
    if (qApp->arguments().contains("--connect"))
    {
        shouldConnect = true;
    }
    if (shouldConnect)
    {
        const QString profileId = activeTabProfileId();
        if (tabControllers.contains(profileId))
        {
            tabWidgets[profileId].connectButton->click();
        }
    }

    if (settings->value("Common/CheckUpdateAfterStart", true).toBool())
    {
        updateChecker->check();
    }
    else
    {
        updateChecker->markDisabled();
    }

    if (!profileService->silentStartEnabled())
    {
        show();
        if (isFirstLaunch)
        {
            QTimer::singleShot(
                0,
                this,
                &MainWindow::promptFirstLaunchGuide
            );
        }
    }
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    if (sessionManager->anyActive())
    {
        event->ignore();
        hide();
        showNotification("EZ4Connect", "程序已最小化到系统托盘，单击图标可恢复窗口。", QSystemTrayIcon::MessageIcon::Information);
    }
    else
    {
        event->accept();
    }
}

void MainWindow::changeEvent(QEvent *event)
{
    if (event->type() == QEvent::WindowStateChange)
    {
        QWindowStateChangeEvent *stateChangeEvent = static_cast<QWindowStateChangeEvent *>(event);
        if (windowState().testFlag(Qt::WindowMinimized) == true && !(stateChangeEvent->oldState() & Qt::WindowMinimized))
        {
            event->ignore();
            hide();
            showNotification("EZ4Connect", "程序已最小化到系统托盘，单击图标可恢复窗口。", QSystemTrayIcon::MessageIcon::Information);
        }
    }
    else
    {
        event->accept();
    }
}

void MainWindow::clearLog()
{
    ui->logPlainTextEdit->clear();
    ui->logPlainTextEdit->appendPlainText(
        "欢迎使用 " + QApplication::applicationDisplayName() + "\n"
        "当前版本：" + QApplication::applicationVersion() + "\n"
        "系统版本：" + QSysInfo::prettyProductName() + "\n"
        "配置路径：" + settings->fileName() + "\n");
}

void MainWindow::resetZjuConnectUi()
{
    const QString profileId = activeTabProfileId();
    if (tabWidgets.contains(profileId))
    {
        tabWidgets[profileId].connectButton->setText("连接服务器");
        trayConnectAction->setText("连接服务器");
        tabWidgets[profileId].proxyButton->setText("设置系统代理");
        tabWidgets[profileId].proxyButton->hide();
        updateTabConnectionState(profileId, ConnectionState::Disconnected);
    }
    updateProfileSummary();
}

void MainWindow::applyColorScheme(Qt::ColorScheme scheme)
{
    const QString themePath = scheme == Qt::ColorScheme::Dark
        ? QStringLiteral(":/resource/mainwindow-dark.qss")
        : QStringLiteral(":/resource/mainwindow-light.qss");

    QString styleSheet;
    if (!appendStyleSheet(QStringLiteral(":/resource/mainwindow.qss"), styleSheet)
        || !appendStyleSheet(themePath, styleSheet))
    {
        return;
    }

    setStyleSheet(styleSheet);
}

void MainWindow::updateConnectionState(ConnectionState state)
{
    updateTabConnectionState(activeTabProfileId(), state);
}

void MainWindow::updateTabConnectionState(const QString &profileId, ConnectionState state)
{
    if (!tabWidgets.contains(profileId))
    {
        return;
    }

    const ConnectionTabWidgets &w = tabWidgets[profileId];

    QString propertyValue;
    QString title;
    QString detail;

    switch (state)
    {
    case ConnectionState::Disconnected:
        propertyValue = "disconnected";
        title = "尚未连接";
        detail = "连接后即可访问网络资源。";
        break;
    case ConnectionState::Starting:
        propertyValue = "starting";
        title = "正在连接";
        detail = "正在启动核心并建立安全通道。";
        break;
    case ConnectionState::Running:
        propertyValue = "running";
        title = "已连接";
        detail = systemProxySession->isEnabled()
            ? "VPN 通道与系统代理均已启用。"
            : "VPN 通道运行中，可按需启用系统代理。";
        break;
    case ConnectionState::Stopping:
        propertyValue = "stopping";
        title = "正在断开";
        detail = "正在安全关闭当前连接。";
        break;
    case ConnectionState::Reconnecting:
        propertyValue = "reconnecting";
        title = "正在重连";
        detail = "连接中断，正在按当前策略重新尝试。";
        break;
    case ConnectionState::Interrupted:
        propertyValue = "failed";
        title = "连接已断开";
        detail = "VPN 核心意外退出，请查看右侧日志。";
        break;
    case ConnectionState::Failed:
        propertyValue = "failed";
        title = "连接失败";
        detail = "请查看右侧日志，确认网络与账户配置。";
        break;
    }

    w.statusTitleLabel->setText(title);
    w.statusDetailLabel->setText(detail);

    const QList<QWidget *> styledWidgets{
        w.statusIndicator,
        w.connectButton
    };
    for (QWidget *widget : styledWidgets)
    {
        widget->setProperty("connectionState", propertyValue);
        widget->style()->unpolish(widget);
        widget->style()->polish(widget);
        widget->update();
    }
}

void MainWindow::updateProfileSummary()
{
    const QString profileId = activeTabProfileId();
    if (!tabWidgets.contains(profileId))
    {
        return;
    }

    QSettings *profileSettings = settingsForProfile(profileId);

    const ConnectionTabWidgets &w = tabWidgets[profileId];
    const QString profileName = profileId.isEmpty()
        ? QStringLiteral("默认")
        : profileId;
    const QString protocolSetting = profileSettings->value(
        "ZJUConnect/Protocol",
        "easyconnect"
    ).toString();
    const QString protocol = protocolSetting.compare(
        "atrust",
        Qt::CaseInsensitive
    ) == 0 ? QStringLiteral("aTrust") : QStringLiteral("EasyConnect");
    const QString server = profileSettings->value(
        "ZJUConnect/ServerAddress"
    ).toString().trimmed();

    QStringList details{protocol};
    details.append(server.isEmpty() ? QStringLiteral("尚未配置服务器") : server);

    w.profileNameLabel->setText(profileName);
    w.profileDetailLabel->setText(details.join(QStringLiteral(" · ")));
}

void MainWindow::setupTrayIcon()
{
    trayIcon = new QSystemTrayIcon(this);
    trayIcon->setIcon(
        QIcon(QPixmap(":/resource/icon.png").scaled(512, 512, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
    trayIcon->setToolTip(QApplication::applicationName());
    connect(trayIcon, &QSystemTrayIcon::activated, this, [&](QSystemTrayIcon::ActivationReason reason) {
        switch (reason)
        {
        case QSystemTrayIcon::Context:
            break;
        default:
            if (isHidden())
            {
                show();
            }
            setWindowState(Qt::WindowState::WindowActive);
            setFocus();
            break;
        }
    });

    trayConnectAction = new QAction("连接服务器", this);
    trayProfileMenu = new QMenu("配置选择", this);
    trayShowAction = new QAction("显示主界面", this);
    trayCloseAction = new QAction("退出 " + QApplication::applicationName(), this);
    trayMenu = new QMenu(this);
    trayMenu->addAction(trayConnectAction);
    trayMenu->addSeparator();
    trayMenu->addMenu(trayProfileMenu);
    trayMenu->addSeparator();
    trayMenu->addAction(trayShowAction);
    trayMenu->addAction(trayCloseAction);
    connect(trayConnectAction, &QAction::triggered, this, [&]() {
        const QString profileId = activeTabProfileId();
        if (tabWidgets.contains(profileId))
        {
            tabWidgets[profileId].connectButton->click();
        }
    });
    connect(trayShowAction, &QAction::triggered, this, [&]() {
        show();
        setWindowState(Qt::WindowState::WindowActive);
        setFocus();
    });
    connect(trayCloseAction, &QAction::triggered, this, &MainWindow::gracefullyQuit);
    trayIcon->setContextMenu(trayMenu);
    trayIcon->show();
}

void MainWindow::setupProfileMenu()
{
    ui->profileMenu->addSeparator();
    newProfileAction = ui->profileMenu->addAction("新建配置");
    renameProfileAction = ui->profileMenu->addAction("重命名当前配置");
    deleteProfileAction = ui->profileMenu->addAction("删除当前配置");
    ui->profileMenu->addSeparator();

    connect(
        ui->configurationGuideAction,
        &QAction::triggered,
        this,
        &MainWindow::openConfigurationGuide
    );
    connect(newProfileAction, &QAction::triggered, this, &MainWindow::createProfile);
    connect(renameProfileAction, &QAction::triggered, this, &MainWindow::renameCurrentProfile);
    connect(deleteProfileAction, &QAction::triggered, this, &MainWindow::deleteCurrentProfile);

    refreshProfileMenu();
}

void MainWindow::refreshProfileMenu()
{
    const QList<QAction *> actions = ui->profileMenu->actions();
    bool remove = false;
    for (QAction *action : actions)
    {
        if (action == ui->configurationGuideAction
            || action == newProfileAction
            || action == renameProfileAction
            || action == deleteProfileAction)
        {
            continue;
        }
        if (action->isSeparator())
        {
            remove = true;
            continue;
        }
        if (remove)
        {
            ui->profileMenu->removeAction(action);
            delete action;
        }
    }

    if (trayProfileMenu != nullptr)
    {
        trayProfileMenu->clear();
    }

    const QString activeId = activeTabProfileId();

    // 默认配置项
    QAction *action = ui->profileMenu->addAction("默认");
    action->setCheckable(true);
    action->setChecked(activeId.isEmpty());
    connect(action, &QAction::triggered, this, [this]()
    {
        switchProfile("");
    });
    if (trayProfileMenu != nullptr)
    {
        QAction *trayAction = trayProfileMenu->addAction("默认");
        trayAction->setCheckable(true);
        trayAction->setChecked(activeId.isEmpty());
        connect(trayAction, &QAction::triggered, this, [this]()
        {
            switchProfile("");
        });
    }

    for (const QString &profileId : profileService->profiles())
    {
        QAction *profileAction = ui->profileMenu->addAction(profileId);
        profileAction->setCheckable(true);
        profileAction->setChecked(profileId == activeId);
        connect(profileAction, &QAction::triggered, this, [this, profileId]()
        {
            switchProfile(profileId);
        });

        if (trayProfileMenu != nullptr)
        {
            QAction *trayAction = trayProfileMenu->addAction(profileId);
            trayAction->setCheckable(true);
            trayAction->setChecked(profileId == activeId);
            connect(trayAction, &QAction::triggered, this, [this, profileId]()
            {
                switchProfile(profileId);
            });
        }
    }

    const bool currentProfileIsManaged =
        !activeId.isEmpty()
        && !profileService->usesOverrideConfiguration();
    renameProfileAction->setEnabled(currentProfileIsManaged);
    deleteProfileAction->setEnabled(currentProfileIsManaged);
}

void MainWindow::setupConnectionTabs()
{
    // Ensure the default profile tab exists.
    addConnectionTab("", true);

    for (const QString &profileId : profileService->profiles())
    {
        addConnectionTab(profileId, false);
    }

    // When the active tab changes, refresh the tray connect action text.
    connect(ui->connectionTabs, &QTabWidget::currentChanged, this,
        [this](int)
        {
            const QString profileId = activeTabProfileId();
            if (tabControllers.contains(profileId))
            {
                tabControllers[profileId]->refreshButtonState();
            }
            refreshProfileMenu();
            updateProfileSummary();
        });
}

void MainWindow::buildTabPage(const QString &profileId, ConnectionTabWidgets &widgets)
{
    widgets.page = new QWidget(ui->connectionTabs);
    auto *layout = new QVBoxLayout(widgets.page);
    layout->setSpacing(12);
    layout->setContentsMargins(16, 16, 16, 16);

    // 状态行
    auto *statusLayout = new QHBoxLayout;
    statusLayout->setSpacing(9);
    widgets.statusIndicator = new QLabel(widgets.page);
    widgets.statusIndicator->setMinimumSize(10, 10);
    widgets.statusIndicator->setMaximumSize(10, 10);
    widgets.statusIndicator->setObjectName("statusIndicator");
    statusLayout->addWidget(widgets.statusIndicator);

    widgets.statusTitleLabel = new QLabel("尚未连接", widgets.page);
    widgets.statusTitleLabel->setObjectName("statusTitleLabel");
    statusLayout->addWidget(widgets.statusTitleLabel);
    statusLayout->addStretch();
    layout->addLayout(statusLayout);

    widgets.statusDetailLabel = new QLabel("连接后即可访问网络资源。", widgets.page);
    widgets.statusDetailLabel->setObjectName("statusDetailLabel");
    widgets.statusDetailLabel->setWordWrap(true);
    layout->addWidget(widgets.statusDetailLabel);

    widgets.connectButton = new QPushButton("连接服务器", widgets.page);
    widgets.connectButton->setObjectName("connectButton");
    widgets.connectButton->setMinimumHeight(48);
    widgets.connectButton->setDefault(true);
    layout->addWidget(widgets.connectButton);

    widgets.proxyButton = new QPushButton("设置系统代理", widgets.page);
    widgets.proxyButton->setObjectName("proxyButton");
    widgets.proxyButton->setMinimumHeight(42);
    widgets.proxyButton->hide();
    layout->addWidget(widgets.proxyButton);

    // 分割线
    auto *divider = new QFrame(widgets.page);
    divider->setFrameShape(QFrame::HLine);
    divider->setFrameShadow(QFrame::Sunken);
    layout->addWidget(divider);

    widgets.profileNameLabel = new QLabel("默认", widgets.page);
    widgets.profileNameLabel->setObjectName("profileNameLabel");
    layout->addWidget(widgets.profileNameLabel);

    widgets.profileDetailLabel = new QLabel("EasyConnect · 等待连接", widgets.page);
    widgets.profileDetailLabel->setObjectName("profileDetailLabel");
    widgets.profileDetailLabel->setWordWrap(true);
    layout->addWidget(widgets.profileDetailLabel);

    layout->addStretch();

    const QString tabTitle = profileId.isEmpty()
        ? QStringLiteral("默认")
        : profileId;
    ui->connectionTabs->addTab(widgets.page, tabTitle);
}

void MainWindow::addConnectionTab(const QString &profileId, bool makeCurrent)
{
    if (tabWidgets.contains(profileId))
    {
        return;
    }

    ConnectionTabWidgets widgets;
    buildTabPage(profileId, widgets);
    tabWidgets.insert(profileId, widgets);

    // Determine base ports from this profile's own settings.
    QSettings *profileSettings = settingsForProfile(profileId);
    const int baseSocks = profileSettings->value("ZJUConnect/SOCKS5Port", 11080).toInt();
    const int baseHttp = profileSettings->value("ZJUConnect/HTTPPort", 11081).toInt();

    ConnectionSession *session = sessionManager->sessionFor(profileId, baseSocks, baseHttp);
    coordinator->wireSession(profileId, session);

    // Wire session state changes to the tab's UI.
    connect(session, &ConnectionSession::stateChanged, this,
        [this, profileId](ConnectionState state)
        {
            updateTabConnectionState(profileId, state);
            if (profileId == activeTabProfileId())
            {
                trayConnectAction->setText(
                    state == ConnectionState::Running
                        || state == ConnectionState::Starting
                        || state == ConnectionState::Reconnecting
                        ? "断开服务器"
                        : "连接服务器"
                );
            }
        });

    auto *controller = new ConnectionUiController(
        this,
        widgets.connectButton,
        widgets.proxyButton,
        trayConnectAction,
        session,
        systemProxySession,
        authenticationDialogs,
        applicationLogger,
        [this, profileId]() -> QSettings *
        {
            return settingsForProfile(profileId);
        },
        profileId,
        [this](
            const QString &title,
            const QString &content,
            QSystemTrayIcon::MessageIcon icon
        )
        {
            showNotification(title, content, icon);
        },
        this
    );
    tabControllers.insert(profileId, controller);

    if (makeCurrent)
    {
        const int index = ui->connectionTabs->indexOf(widgets.page);
        if (index >= 0)
        {
            ui->connectionTabs->setCurrentIndex(index);
        }
    }

    updateProfileSummary();
    updateTabConnectionState(profileId, session->state());
}

QString MainWindow::activeTabProfileId() const
{
    QWidget *current = ui->connectionTabs->currentWidget();
    for (auto it = tabWidgets.cbegin(); it != tabWidgets.cend(); ++it)
    {
        if (it.value().page == current)
        {
            return it.key();
        }
    }
    return QString();
}

QSettings *MainWindow::settingsForProfile(const QString &profileId)
{
    if (profileId.isEmpty())
    {
        return settings;
    }
    if (profileSettings.contains(profileId))
    {
        return profileSettings.value(profileId);
    }
    QSettings *s = profileService->settingsFor(profileId);
    if (s == nullptr)
    {
        return settings;
    }
    profileSettings.insert(profileId, s);
    return s;
}

bool MainWindow::switchProfile(const QString &profileId)
{
    if (!tabWidgets.contains(profileId))
    {
        addConnectionTab(profileId, true);
    }

    const ConnectionTabWidgets &widgets = tabWidgets[profileId];
    const int index = ui->connectionTabs->indexOf(widgets.page);
    if (index >= 0)
    {
        ui->connectionTabs->setCurrentIndex(index);
    }
    else
    {
        ui->connectionTabs->setCurrentWidget(widgets.page);
    }

    refreshProfileMenu();
    updateProfileSummary();
    qInfo().noquote() << "已切换到配置：" + (profileId.isEmpty() ? "默认" : profileId);
    return true;
}

void MainWindow::createProfile()
{
    bool ok = false;
    const QString name = QInputDialog::getText(
        this,
        "新建配置",
        "请输入配置名称：\n（仅支持字母、数字、下划线和连字符）",
        QLineEdit::Normal,
        "",
        &ok
    );
    if (!ok)
    {
        return;
    }

    if (settingWindow != nullptr)
    {
        settingWindow->close();
    }

    const QString newProfileId = profileService->createAndSwitch(name);
    if (newProfileId.isEmpty())
    {
        QMessageBox::critical(this, "创建失败", "无法创建新配置。");
        return;
    }

    settings = profileService->settings();
    authenticationDialogs->setSettings(settings);
    upgradeSettings();
    updateVersionInfo();

    addConnectionTab(newProfileId, true);
    refreshProfileMenu();

    promptConfigurationGuide(
        "配置已创建",
        "配置 \"" + newProfileId + "\" 已创建，是否现在使用配置引导完成设置？"
    );
}

void MainWindow::openConfigurationGuide()
{
    if (settingWindow != nullptr)
    {
        settingWindow->close();
    }

    const QString profileId = activeTabProfileId();
    QSettings *profileSettings = settingsForProfile(profileId);

    const QString oldServerAddress =
        profileSettings->value("ZJUConnect/ServerAddress").toString();
    const int oldServerPort =
        profileSettings->value("ZJUConnect/ServerPort").toInt();
    const QString oldProtocol =
        profileSettings->value("ZJUConnect/Protocol").toString();
    const QString oldAuthType =
        profileSettings->value("ZJUConnect/AuthType").toString();
    const QString oldEasyConnectAuthType =
        profileSettings->value("ZJUConnect/EasyConnectAuthType").toString();
    const QString oldLoginDomain =
        profileSettings->value("ZJUConnect/LoginDomain").toString();
    const QString oldLoginUrl =
        profileSettings->value("ZJUConnect/LoginURL").toString();

    ConfigurationGuideDialog guide(this, profileSettings);
    if (guide.exec() != QDialog::Accepted)
    {
        return;
    }
    guide.applyTo(*profileSettings);
    profileSettings->sync();

    const bool authenticationSettingsChanged =
        oldServerAddress != profileSettings->value("ZJUConnect/ServerAddress").toString()
        || oldServerPort != profileSettings->value("ZJUConnect/ServerPort").toInt()
        || oldProtocol != profileSettings->value("ZJUConnect/Protocol").toString()
        || oldAuthType != profileSettings->value("ZJUConnect/AuthType").toString()
        || oldEasyConnectAuthType
            != profileSettings->value("ZJUConnect/EasyConnectAuthType").toString()
        || oldLoginDomain != profileSettings->value("ZJUConnect/LoginDomain").toString()
        || oldLoginUrl != profileSettings->value("ZJUConnect/LoginURL").toString();
    if (authenticationSettingsChanged)
    {
        ApplicationPaths::clearClientData(profileId);
    }

    resetZjuConnectUi();
    clearLog();
    qInfo().noquote() << "已通过配置引导更新当前配置";
}

void MainWindow::promptFirstLaunchGuide()
{
    promptConfigurationGuide(
        "欢迎使用 EZ4Connect",
        "检测到这是首次启动，是否现在配置 VPN 服务器？"
    );
}

void MainWindow::promptConfigurationGuide(
    const QString &windowTitle,
    const QString &text
)
{
    QMessageBox messageBox(this);
    messageBox.setWindowTitle(windowTitle);
    messageBox.setText(text);
    messageBox.setInformativeText(
        "配置引导将协助你选择协议、填写服务器地址、认证方式和登录凭据。"
    );

    QPushButton *startButton = messageBox.addButton(
        "使用配置引导",
        QMessageBox::AcceptRole
    );
    messageBox.addButton("稍后配置", QMessageBox::RejectRole);
    messageBox.setDefaultButton(startButton);
    messageBox.exec();

    if (messageBox.clickedButton() == startButton)
    {
        openConfigurationGuide();
    }
}

void MainWindow::renameCurrentProfile()
{
    const QString profileId = activeTabProfileId();
    if (profileId.isEmpty())
    {
        QMessageBox::warning(this, "重命名失败", "默认配置不可重命名。");
        return;
    }

    bool ok = false;
    QString name = QInputDialog::getText(this, "重命名配置", "请输入新配置名称：\n（仅支持字母、数字、下划线）", QLineEdit::Normal, profileId, &ok);
    if (!ok)
    {
        return;
    }

    const QString normalizedName = profileService->normalizeProfileId(name);
    if (normalizedName.isEmpty())
    {
        QMessageBox::warning(this, "重命名失败", "配置名称不能为空。");
        return;
    }
    if (normalizedName == profileId)
    {
        return;
    }

    if (!profileService->switchTo(profileId))
    {
        return;
    }

    if (settingWindow != nullptr)
    {
        settingWindow->close();
    }
    if (!profileService->renameCurrent(normalizedName))
    {
        QMessageBox::warning(this, "重命名失败", "目标配置已存在，或当前配置不可重命名。");
        return;
    }

    settings = profileService->settings();
    authenticationDialogs->setSettings(settings);
    updateVersionInfo();
    refreshProfileMenu();
    clearLog();
    qInfo().noquote() << "当前配置已重命名为：" + normalizedName;
}

void MainWindow::deleteCurrentProfile()
{
    const QString profileId = activeTabProfileId();
    if (profileId.isEmpty())
    {
        QMessageBox::warning(this, "删除失败", "默认配置不可删除。");
        return;
    }

    ConnectionSession *session = sessionManager->find(profileId);
    if (session != nullptr && session->isActive())
    {
        QMessageBox::warning(this, "删除失败", "请先断开该配置的 VPN 连接，再删除配置。");
        return;
    }

    QMessageBox messageBox(this);
    messageBox.setWindowTitle("删除配置");
    messageBox.setText("确认删除配置 \"" + profileId + "\" 吗？");
    messageBox.addButton(QMessageBox::Yes)->setText("是");
    messageBox.addButton(QMessageBox::No)->setText("否");
    messageBox.setDefaultButton(QMessageBox::No);
    if (messageBox.exec() != QMessageBox::Yes)
    {
        return;
    }

    if (settingWindow != nullptr)
    {
        settingWindow->close();
    }

    if (tabWidgets.contains(profileId))
    {
        ui->connectionTabs->removeTab(
            ui->connectionTabs->indexOf(tabWidgets[profileId].page)
        );
        delete tabWidgets[profileId].page;
        tabWidgets.remove(profileId);
    }
    if (tabControllers.contains(profileId))
    {
        delete tabControllers[profileId];
        tabControllers.remove(profileId);
    }
    if (profileSettings.contains(profileId))
    {
        delete profileSettings[profileId];
        profileSettings.remove(profileId);
    }

    if (!profileService->switchTo(profileId))
    {
        refreshProfileMenu();
        return;
    }
    if (!profileService->removeCurrentAndSwitchToDefault())
    {
        QMessageBox::warning(this, "删除失败", "无法删除该配置文件。");
        refreshProfileMenu();
        return;
    }

    settings = profileService->settings();
    authenticationDialogs->setSettings(settings);
    upgradeSettings();
    updateVersionInfo();
    resetZjuConnectUi();
    clearLog();
    refreshProfileMenu();
    qInfo().noquote() << "已删除配置：" + profileId;
}

void MainWindow::upgradeSettings()
{
    const SettingsMigrationAction action = SettingsMigrator::prepare(*settings);
    if (action == SettingsMigrationAction::MigrateAutoStart)
    {
        profileService->migrateAutoStartSetting(
            settings->value("Common/AutoStart", false).toBool()
        );
    }
    else if (action == SettingsMigrationAction::RecommendReset)
    {
        QMessageBox msgBox;
        msgBox.setText("存在配置更新");
        msgBox.setInformativeText("建议恢复默认设置，以使用优化的配置。\n\n是否恢复默认设置？");
        msgBox.setStandardButtons(QMessageBox::Ok | QMessageBox::Cancel);
        msgBox.setDefaultButton(QMessageBox::Cancel);

        const bool reset = msgBox.exec() == QMessageBox::Ok;
        SettingsMigrator::finish(*settings, reset);
        if (reset)
        {
            QMessageBox::information(this, "完成", "已恢复默认设置。");
        }
        return;
    }
    SettingsMigrator::finish(*settings, false);
}

void MainWindow::updateVersionInfo()
{
    const VersionInfo &versionInfo = updateChecker->versionInfo();
    ui->versionLabel->setText("版本 " + versionInfo.uiVersion);
    ui->versionLabel->setToolTip(
        "UI 版本：" + versionInfo.uiVersion + "（最新：" + versionInfo.uiLatest + "）\n"
        "核心版本：" + versionInfo.coreVersion + "（最新：" + versionInfo.coreLatest + "）"
    );
    updateProfileSummary();
}

void MainWindow::showNotification(const QString &title, const QString &content, QSystemTrayIcon::MessageIcon icon)
{
    disconnect(trayIcon, &QSystemTrayIcon::messageClicked, nullptr, nullptr);
    trayIcon->showMessage(
        title,
        content,
        icon,
        10000
    );

    connect(trayIcon, &QSystemTrayIcon::messageClicked, this, [&]()
    {
        disconnect(trayIcon, &QSystemTrayIcon::messageClicked, nullptr, nullptr);

        show();
        setWindowState(Qt::WindowState::WindowActive);
    });
}

void MainWindow::cleanUpWhenQuit()
{
    // 保存配置
    if (settings->value("Common/ConfigVersion", 0).toInt() <=
        ApplicationConstants::ConfigVersion)
    {
        settings->setValue(
            "Common/ConfigVersion",
            ApplicationConstants::ConfigVersion
        );
    }
    settings->sync();

    // 清除系统代理
    coordinator->prepareForShutdown();
}

void MainWindow::gracefullyQuit()
{
    if (sessionManager->anyActive())
    {
        connect(sessionManager, &ConnectionSessionManager::activeChanged, this,
            [this](const QString &, bool active)
            {
                if (!active && !sessionManager->anyActive())
                {
                    QApplication::quit();
                }
            });
        sessionManager->stopAll();
    }
    else
    {
        qApp->quit();
    }
}

MainWindow::~MainWindow()
{
    delete coordinator;
    coordinator = nullptr;

    qDeleteAll(profileSettings);
    profileSettings.clear();

    QObject::disconnect(applicationLogger, nullptr, this, nullptr);
    delete ui;
    ui = nullptr;
}
