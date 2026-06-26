# KDE Uninstall

KDE Uninstall 是一个给 KDE Plasma 应用启动器添加“卸载”右键按钮的小工具。

运行安装后，它会给 KDE Plasma 的应用启动器安装一个专用动作。之后在 KDE 应用启动器里右键应用，就可以看到“卸载”按钮。点击后会弹出确认框，再调用对应的包管理器卸载应用。

新版不会再给每个应用 `.desktop` 文件写入 `Actions=`，所以不会在底部任务管理器或图标任务管理器的右键菜单里显示“卸载”。如果之前运行过旧版安装，新版 `--install` 会自动清理旧版写入的任务栏菜单项。

## 支持的应用来源

当前会尝试识别并卸载这些来源的应用：

- Flatpak
- Snap
- pacman
- apt / dpkg
- rpm / dnf / zypper
- xbps
- apk
- eopkg
- AppImage（例如 `~/Applications/xxx.AppImage`、`/opt/xxx/xxx.AppImage`）
- 手动复制到 `/opt/<app-dir>` 的应用（例如 `/opt/clion/bin/clion.sh`、`/opt/idea/bin/idea.sh`）

无法安全识别来源的应用不会被卸载。

对于 AppImage，卸载计划会删除 AppImage 文件、对应的 `.desktop` 启动器，以及能安全确认属于该应用的图标文件。

对于手动复制到 `/opt` 的应用，程序会从 `.desktop` 的 `Exec=` 反推出应用目录，只在满足保守条件时删除 `/opt/<app-dir>`：路径必须位于 `/opt` 下，不能是 `/opt` 本身或 `/opt/bin`、`/opt/share` 等公共目录，目录名需要像应用目录，目录内也需要有应用标识（例如 `bin/`、`product-info.json`、`AppRun` 或同名可执行文件）。危险路径如 `/opt`、`/usr`、`/usr/local`、`/home` 会被拒绝。

这两类手动安装应用不会默认删除 `~/.config/<app>`、`~/.local/share/<app>`、`~/.cache/<app>` 等用户数据目录。

## 构建

```bash
cmake -S . -B build
cmake --build build -j2
```

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

这个项目不会修改 KDE Plasma 源码，也不会直接写系统应用目录。它只写当前用户目录下的文件。

“卸载”按钮通过 Plasma 的 `plasma/kickeractions` 入口添加，只面向 KDE 应用启动器菜单。不同启动器样式中的具体排序仍由 Plasma 决定。
