# KDE Uninstall

KDE Uninstall 是一个给 KDE Plasma 应用启动器添加“卸载”右键按钮的小工具。

运行安装后，它会给 KDE Plasma 的应用启动器安装一个专用动作。之后在 KDE 应用启动器里右键应用，就可以看到“卸载”按钮。点击后会弹出确认框，再调用对应的包管理器卸载应用。

新版不会再给每个应用 `.desktop` 文件写入 `Actions=`，所以不会在底部任务管理器或图标任务管理器的右键菜单里显示“卸载”。如果之前运行过旧版安装，新版 `--install` 会自动清理旧版写入的任务栏菜单项。

## 支持的应用来源

识别顺序如下，命中一个就停止：

1. **Flatpak**：`X-Flatpak`、flatpak 导出目录，或 `Exec=flatpak run <id>`
2. **Snap**：`X-SnapInstanceName`、snapd 目录、`/snap/bin/xxx`、`snap run xxx`
3. **Steam 游戏**：`steam://rungameid/<id>`，交给 Steam 执行 `steam://uninstall/<id>`
4. **系统包**：pacman、apt/dpkg、rpm/dnf/zypper、xbps、apk、eopkg。查询启动器本身、它的符号链接目标、`~/.local/share/applications` 中覆盖的同名系统启动器、`TryExec` 以及实际程序文件
5. **浏览器网页应用**：带 `--app-id=`/`--app=` 的启动器只删启动器和图标，不会卸载浏览器
6. **Wine 程序**：打开对应前缀的 `wine uninstaller`
7. **pipx**：程序位于 pipx venv 中时执行 `pipx uninstall`
8. **AppImage**：家目录、`/opt`、`/usr/local` 下任意位置的 `.AppImage`
9. **手动安装目录**：`/opt`、`/usr/local`、`~/.local/share`、`~/.local/opt`、`~/Applications`、`~/apps`、`~/opt`、`~/Programs`、`~/Games`、下载/文档等 XDG 目录，以及家目录下的普通目录。Wine 的 `Program Files` 和 Steam 的 `steamapps/common` 也按容器处理
10. **单个程序文件**：例如 `~/.local/bin/foo`、`/usr/local/bin/foo`，以及无法确定所属目录的情况
11. **启动器残留**：程序已经不存在、`Exec=` 无法解析，或程序位于系统目录但没有包归属时，仍然删除启动器、图标和匹配的用户数据

`java -jar x.jar`、`python3 x.py`、`node`、`electron`、`wine x.exe`、`appimage-run` 等解释器启动的应用，会以被运行的文件作为卸载目标，**不会**把 java、python、wine 等运行时当作应用卸载。`gamemoderun`、`prime-run`、`mangohud`、`firejail` 等包装命令会被跳过。

`Exec=` 只做静态解析，不会执行。

### 手动安装应用的删除范围

- 应用目录：带 `bin`/`libexec`/`sbin` 布局时取其上一级；在 Google、JetBrains、Microsoft、apps、common、games、Program Files 等多应用容器里，取容器下面那一级；其他情况取容器下的第一级目录
- 指向应用的其他启动器（应用菜单、`~/.config/autostart`、桌面目录），按实际目标匹配
- `~/.local/bin`、`~/bin`、`/usr/local/bin`、`/usr/local/sbin`、`/usr/bin`、`/opt/bin` 中指向应用的命令软链接
- `hicolor` 图标主题和 `pixmaps` 中名称匹配、且不属于任何包的图标。其他图标主题包（Tela、WhiteSur 等）不会动
- **用户数据**：`~/.config`、`~/.cache`、`~/.local/state`、`~/.local/share` 下与应用名**完全相同**的条目，`~/.<应用名>` 隐藏目录，以及 JetBrains 等厂商目录下的同名产品目录（如 `~/.config/JetBrains/CLion2026.3`）

应用名来自启动器文件名、反向域名 ID 的最后一段、`Name=`、`StartupWMClass=`、程序文件名和安装目录名，会去掉版本号和架构后缀。

### 确认与安全边界

确认框是一个勾选列表，所有项目默认勾选，不想删的取消勾选即可。取消勾选应用本体时，指向它的命令链接会一并保留。

删除由程序自身完成（需要时通过 pkexec 提权），执行前会再次逐项校验：

- 家目录、`~/.config`、`~/.local`、`~/.ssh`、`~/.gnupg`、XDG 用户目录、`/opt`、`/usr/local` 本身，以及各类容器目录都不会被整体删除
- 属于系统包的文件和目录会被拒绝
- 不会跟随符号链接跳出允许的目录
- `applications`、`icons`、`fonts`、`kde*`、`plasma*`、`gtk*`、`flatpak`、`Steam`、`JetBrains` 等公共或厂商目录不会被当作用户数据删除

## 构建

```bash
cmake -S . -B build
cmake --build build -j2
ctest --test-dir build --output-on-failure
```

测试覆盖 AppImage、家目录手动安装、解释器负载、网页应用、Steam/Wine/Flatpak 启动器、启动器残留、用户数据匹配和路径校验。如果系统安装了 `bwrap`，还会在隔离的 `/opt`、启动器目录和命令目录中执行手动安装应用的完整卸载测试；这部分需要允许用户命名空间。

## 添加“卸载”按钮

```bash
./build/kde-uninstall --install
```

执行后程序会写入：

- `~/.local/bin/kde-uninstall`
- `~/.local/share/plasma/kickeractions/kde-uninstall.desktop`

然后打开 KDE 应用启动器，右键某个应用，菜单中会出现“卸载”。底部任务管理器或图标任务管理器的右键菜单不会显示这个按钮。

如果菜单没有立即刷新，可以运行：

```bash
kbuildsycoca6 --noincremental
```

KDE 5 使用：

```bash
kbuildsycoca5 --noincremental
```

## 移除“卸载”按钮

```bash
~/.local/bin/kde-uninstall --restore
```

这会删除本工具安装的应用启动器动作，并清理旧版曾经写入的用户级 `.desktop` 动作。

## 使用说明

正常使用只需要运行一次安装命令：

```bash
./build/kde-uninstall --install
```

之后通过 KDE 应用启动器右键菜单点击“卸载”即可。

卸载前会弹出确认框，确认后才会执行实际卸载命令。

## 注意事项

安装菜单集成只写当前用户目录，不修改 KDE Plasma 源码。卸载手动安装应用时，可能需要通过 `pkexec` 提权，以删除确认列表中的 `/opt` 应用目录及系统级启动器、命令链接或图标。

“卸载”按钮通过 Plasma 的 `plasma/kickeractions` 入口添加，只面向 KDE 应用启动器菜单。不同启动器样式中的具体排序仍由 Plasma 决定。
