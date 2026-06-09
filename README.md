# KDE Uninstall

KDE Uninstall 是一个给 KDE Plasma 应用启动器添加“卸载”右键按钮的小工具。

运行安装后，它会扫描系统里的应用 `.desktop` 文件，在用户目录生成或更新对应的启动器条目，并添加一个“卸载”动作。之后在 KDE 应用启动器里右键应用，就可以看到“卸载”按钮。点击后会弹出确认框，再调用对应的包管理器卸载应用。

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

无法安全识别来源的应用不会被卸载。

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
- `~/.local/share/applications/`

然后打开 KDE 应用启动器，右键某个应用，菜单中会出现“卸载”。

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

这会删除本工具生成的用户级启动器副本，或从用户自己的 `.desktop` 文件中移除本工具添加的动作。

## 使用说明

正常使用只需要运行一次安装命令：

```bash
./build/kde-uninstall --install
```

之后通过 KDE 应用启动器右键菜单点击“卸载”即可。

卸载前会弹出确认框，确认后才会执行实际卸载命令。

## 注意事项

这个项目不会修改 KDE Plasma 源码，也不会直接写系统应用目录。它只写当前用户目录下的文件。

“卸载”按钮的位置由 KDE Plasma 启动器决定。当前方式可以把按钮添加到应用右键菜单中，但不能把它移动到 Plasma 自带菜单项，例如“固定到任务管理器”下面。
