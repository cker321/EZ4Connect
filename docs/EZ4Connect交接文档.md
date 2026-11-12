# EZ4Connect 项目交接文档

> 用途：交付给同事继续开发。本文档已脱敏，不含任何账号、密码、服务器地址或内网 IP。

## 1. 项目概览

- **项目名**：EZ4Connect
- **类型**：macOS VPN 客户端
- **技术栈**：Qt 6（C++）+ 内核 `zju-connect`（Go 编写）
- **协议支持**：aTrust、EasyConnect
- **内核仓库**：https://github.com/cker321/zju-connect
- **UI 仓库**：https://github.com/cker321/EZ4Connect（主开发仓库，多连接代码已推送至 `master`）
- **UI 上游参考**：https://github.com/chenx-dust/EZ4Connect（原始项目）
- **本地工作目录**：`EZ4Connect/ui-src/`
- **git remote**：`origin` → chenx-dust/EZ4Connect（上游）；`mine` → cker321/EZ4Connect（本人仓库）
- **已推送提交**：`fa69f2b` feat: 支持同时连接多个 VPN 服务（标签页式多连接）

### 架构一句话总结

UI（Qt C++）通过 `QProcess` 拉起一个或多个 `zju-connect` 内核子进程，每个内核进程 = 一条独立 VPN 隧道。UI 负责多连接的编排、端口分配、系统代理设置与认证交互。

## 2. 目录结构

```
EZ4Connect/
├── ui-src/                 # Qt6 客户端源码（git 仓库）
│   ├── application/        # 业务层：连接会话、端口分配、profile 服务
│   ├── core/               # 核心数据结构（ConnectionProfile、连接状态机）
│   ├── infrastructure/     # 内核进程管理、系统代理、设置加载
│   ├── presentation/       # UI 层：主窗口、协调器、对话框
│   ├── tests/              # 单元测试
│   └── CMakeLists.txt
├── docs/                   # 设计文档（含本文档）
└── dist/                   # 打包产物（macdeployqt 输出）
```

## 3. 技术架构关键点

### 3.1 内核模型

- 一个 `zju-connect` 进程 = 一条隧道，天然支持多实例
- 只要各实例的 socks/http 端口不冲突，即可并行运行
- UI 通过 `QProcess` 启动内核，`CoreCommandBuilder` 负责拼参数

### 3.2 通信链路

```
UI (Qt) ──QProcess──> zju-connect 内核进程
                        └── 监听本地 socks5 / http 端口
```

### 3.3 多连接支持（核心改造）

这是本项目最近一次大的改造目标：**支持同时连接多个 VPN 服务**。

- **交互形式**：标签页式（每个 profile 一个 Tab）
- **系统代理**：默认关闭，需手动选择"主连接"
- 关键新增类：
  - `PortAllocator`：从基准端口递增探测空闲端口，为每个 profile 分配唯一 socks/http 端口对
  - `ConnectionSessionManager`：管理 profileId → session 映射，并强制"系统代理互斥"（同一时刻仅一个连接可占用全局系统代理）

## 4. 本次开发已完成的改动

### 新增文件

| 文件 | 职责 |
|---|---|
| `application/portallocator.{h,cpp}` | 端口分配器：基准端口（默认 11080/11081）递增探测空闲端口 |
| `application/connectionsessionmanager.{h,cpp}` | 会话管理器：profileId→session 映射 + 系统代理互斥 |

### 修改文件（要点）

| 文件 | 改动 |
|---|---|
| `application/connectionsession.{h,cpp}` | 新增 `setAllocatedPorts` / `socksPort` / `httpPort`，支持端口重写 |
| `application/profileservice.{h,cpp}` | 新增 `settingsFor(profileId)` |
| `presentation/main/mainwindow.{h,cpp,ui}` | 控制面板改为 `QTabWidget`，每个 profile 一个 Tab |
| `presentation/main/mainwindowcoordinator.{h,cpp}` | 单 session 改为 `ConnectionSessionManager` + wireSession |
| `presentation/coordinators/connectionuicontroller.{h,cpp}` | 绑定具体 session + profileId |
| `infrastructure/coreprocess/corecommandbuilder.cpp` | 参数名适配旧内核 |
| `infrastructure/coreprocess/zjuconnectprocess.cpp` | tempDir 加 profileId 前缀 |
| `application/defaultsettings.cpp` | `CredentialsAsArguments` 默认 false→true |
| `infrastructure/settings/settingsprofileloader.cpp` | 同步默认值 |
| `presentation/dialogs/settingwindow/settingwindow.cpp` | 同步复选框默认值 |
| `tests/*` | 同步测试 |

### 参数名适配（旧内核 vs 新 UI）

| UI 新参数 | 旧内核实际参数 |
|---|---|
| `-remote-dns-server` | `-zju-dns-server` |
| `-disable-remote-dns` | `-disable-zju-dns` |
| `-local-dns-server` | 无（已删除） |
| `-debug-pcap-file` / `-debug-tls-log-file` | 无（已删除） |

## 5. 开发过程中踩过的坑（重要经验）

1. **`zju-connect: command not found`**：裸编译的 app 不含内核二进制，需手动将内核复制到 `build/EZ4Connect.app/Contents/MacOS/`
2. **`flag provided but not defined: -remote-dns-server`**：UI 与内核参数名不匹配，已按"UI 适配旧内核"方向修正
3. **easyconnect `missing required arguments`**：UI 通过环境变量传凭证，旧内核不读环境变量 → 改为 `CredentialsAsArguments=true`
4. **atrust `sudo: a password is required`**：TUN 模式需 root 权限，测试时先关 TUN
5. **流量未过客户端**：TUN 模式未清理导致残留 utun 孤儿接口 + 大量指向 `198.18.0.1` 的黑洞路由，劫持流量
6. **系统代理端口指向错误**（已修复）：设置系统代理时代码读的是基准端口，而非 PortAllocator 实际分配端口，导致流量导入空端口。修复：改用 session 实际端口

## 6. 遗留待办（TODO）

1. ~~系统代理端口映射接入 PortAllocator 分配结果~~ ✅ 已完成
2. **TUN 模式互斥**：未实现（当前 TUN 模式下多连接会争抢系统路由表）
3. **日志分 Tab**：未实现（各连接日志尚未按 Tab 隔离）
4. **重命名/删除 profile 后 Tab 标题联动**：未实现
5. **多连接"按网段分流"**：当前两个连接无法按目标网段自动各走各的隧道，系统代理只能单点指向。可选方案：内置多路分流代理 / TUN+精确路由，需进一步设计

## 7. 打包与分发（重要）

### 当前问题

- 编译产物 `EZ4Connect.app` 链接的是本机 Homebrew 绝对路径 Qt 库，**直接分享给别人无法运行**（缺依赖崩溃）
- app 体积约 314M，其中 **206M 是 QtWebEngine（Chromium 内核）**，占比 66%

### 已完成的打包步骤

```bash
# 1. 复制 app 到发布目录
cp -R build/EZ4Connect.app dist/

# 2. 用 macdeployqt 打包 Qt 依赖
/opt/homebrew/opt/qt/bin/macdeployqt dist/EZ4Connect.app

# 3. 重新签名（adhoc）
codesign --force --deep --sign - dist/EZ4Connect.app

# 4. 打包 zip
ditto -c -k --keepParent dist/EZ4Connect.app dist/EZ4Connect-arm64.zip
```

### 体积优化方向（重要，尚未实施）

体积大头是 **QtWebEngine**（206M，用于 SSO 网页登录的内嵌浏览器）。优化思路：

- **方案 A（推荐）**：去掉 QtWebEngine，改用系统默认浏览器做 SSO 登录（`QDesktopServices::openUrl`）。项目已有本地回调服务器（`callbackServerUrl`），SSO 最终重定向到 localhost 即可捕获 token，无需内嵌浏览器。体积可降至约 100M。
- **方案 B**：整体换 Electron —— 但 Electron 同样背 Chromium（约 150-200M），不解决体积问题，仅适合长期跨平台诉求
- **方案 C**：整体换 Swift 原生（WKWebView 调系统 WebKit）—— 体积可做到 <20M，但需全量重写

### 分发注意事项

- 当前为 adhoc 签名，对方首次打开需右键 → 打开 → 再点"打开"
- 如需彻底去掉该步骤，需要 Apple 开发者账号做正式签名 + 公证（notarization）
- 应用本身不含用户账号密码（账号密码在运行时由用户输入，存于 `~/Library/Preferences/EZ4Connect/`）

## 8. 编译环境

- **CPU**：Apple Silicon（arm64）
- **Qt**：6.12.0（Homebrew，`/opt/homebrew/opt/qt`）+ WebEngine 6.11.2
- **构建工具**：cmake
- **配置命令**：

```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH=/opt/homebrew/opt/qt
cmake --build build --target EZ4Connect
```

- 单元测试：`corecommandbuilder_test`、`settingsprofileloader_test` 等，均通过

## 9. 关键文件索引

| 关注点 | 文件 |
|---|---|
| 多连接管理入口 | `application/connectionsessionmanager.{h,cpp}` |
| 端口分配 | `application/portallocator.{h,cpp}` |
| 单个连接会话 | `application/connectionsession.{h,cpp}` |
| 系统代理 | `application/systemproxysession.{h,cpp}`、`infrastructure/platform/platformsystemproxybackend.cpp` |
| 内核参数构建 | `infrastructure/coreprocess/corecommandbuilder.cpp` |
| 主窗口（Tab 布局）| `presentation/main/mainwindow.{h,cpp,ui}` |
| 连接 UI 控制器 | `presentation/coordinators/connectionuicontroller.{h,cpp}` |
| SSO 网页登录（体积大头）| `presentation/dialogs/ssologinwebview/` |

## 10. 下一步建议（优先级排序）

1. **去掉 QtWebEngine**，改系统浏览器 SSO，体积 314M → 100M（性价比最高）
2. **实现多连接按网段分流**（内置多路分流代理 或 TUN+精确路由）
3. 补齐 TUN 模式互斥、日志分 Tab、Tab 标题联动等遗留项
4. 如需正式分发，申请 Apple 开发者账号做签名 + 公证
