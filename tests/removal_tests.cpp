#include "kde_uninstall/package_manager.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <unistd.h>
#include <vector>

namespace fs = std::filesystem;
namespace ku = kde_uninstall;

namespace {

void require(bool condition, const std::string &message) {
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void write(const fs::path &path, const std::string &content, bool executable = false) {
    fs::create_directories(path.parent_path());
    std::ofstream output(path);
    output << content;
    output.close();
    require(bool(output), "cannot create fixture: " + path.string());
    if (executable) {
        fs::permissions(path, fs::perms::owner_exec, fs::perm_options::add);
    }
}

struct Fixture {
    fs::path root;
    fs::path home;
    fs::path data;
    fs::path tools;
    std::vector<std::pair<std::string, std::string>> environment;
    std::vector<std::string> unset;

    Fixture() {
        char pattern[] = "/tmp/kde-uninstall-tests-XXXXXX";
        char *created = ::mkdtemp(pattern);
        require(created != nullptr, "mkdtemp failed");
        root = created;
        home = root / "home";
        data = home / ".local/share";
        tools = root / "tools";
        for (const auto &dir : {home / ".local/bin", home / "bin", data / "applications", tools}) {
            fs::create_directories(dir);
        }
        setEnvironment("HOME", home.string());
        setEnvironment("XDG_DATA_HOME", data.string());
        setEnvironment("PATH", tools.string() + ":" + (home / ".local/bin").string() + ":/usr/local/bin");
    }

    void setEnvironment(const std::string &name, const std::string &value) {
        if (const char *old = std::getenv(name.c_str())) {
            environment.emplace_back(name, old);
        } else {
            unset.push_back(name);
        }
        ::setenv(name.c_str(), value.c_str(), 1);
    }

    ~Fixture() {
        for (const auto &entry : environment) {
            ::setenv(entry.first.c_str(), entry.second.c_str(), 1);
        }
        for (const auto &name : unset) {
            ::unsetenv(name.c_str());
        }
        std::error_code ec;
        fs::remove_all(root, ec);
    }

    fs::path desktop(const std::string &name, const std::string &exec, const std::string &extra = "") {
        fs::path path = data / "applications" / name;
        write(path, "[Desktop Entry]\nType=Application\nName=Test App\nExec=" + exec + "\n" + extra);
        return path;
    }
};

bool hasTarget(const ku::RemovalPlan &plan, const std::string &kind, const fs::path &path) {
    return std::any_of(plan.removalTargets.begin(), plan.removalTargets.end(), [&](const auto &target) {
        return target.kind == kind && fs::path(target.path) == path;
    });
}

int remove(const Fixture &fixture, const std::vector<std::pair<std::string, fs::path>> &targets) {
    std::vector<std::string> args = {"removal-tests", "--remove-paths", fixture.home.string(), fixture.data.string()};
    for (const auto &target : targets) {
        args.push_back(target.first);
        args.push_back(target.second.string());
    }
    std::vector<char *> argv;
    for (auto &arg : args) {
        argv.push_back(arg.data());
    }
    return ku::removePlannedPaths(static_cast<int>(argv.size()), argv.data());
}

int removePlan(const Fixture &fixture, const ku::RemovalPlan &plan) {
    std::vector<std::pair<std::string, fs::path>> targets;
    for (const auto &target : plan.removalTargets) {
        targets.emplace_back(target.kind, target.path);
    }
    return remove(fixture, targets);
}

void appImageTests() {
    Fixture fixture;
    auto image = fixture.home / "Applications/Test.AppImage";
    write(image, "#!/bin/sh\nexit 0\n", true);
    auto link = fixture.home / ".local/bin/test-app";
    fs::create_symlink(image, link);
    auto desktop = fixture.desktop("test.desktop", "env DATA=/some/path test-app %U", "Icon=Test\n");
    auto icon = fixture.data / "icons/hicolor/64x64/apps/Test.png";
    write(icon, "icon");
    auto unrelated = fixture.desktop("other.desktop", "/unrelated/program");
    auto config = fixture.home / ".config/Test/settings";
    write(config, "matching user data");
    auto otherConfig = fixture.home / ".config/Other/settings";
    write(otherConfig, "keep unrelated user data");

    auto plan = ku::removalPlanForDesktop(desktop);
    require(plan && plan->manager == "AppImage", "AppImage through env and PATH link not recognized");
    require(hasTarget(*plan, "appimage", image), "resolved AppImage missing");
    require(hasTarget(*plan, "command-link", link), "AppImage command link missing");
    require(hasTarget(*plan, "icon", icon), "AppImage icon missing");
    require(hasTarget(*plan, "data-dir", fixture.home / ".config/Test"), "matching user data missing");
    require(!hasTarget(*plan, "data-dir", fixture.home / ".config/Other"), "unrelated user data selected");
    require(!hasTarget(*plan, "desktop", unrelated), "unrelated desktop selected");
    require(removePlan(fixture, *plan) == 0, "AppImage removal failed");
    require(!fs::exists(image) && !fs::is_symlink(link) && !fs::exists(desktop) && !fs::exists(icon),
            "AppImage files left behind");
    require(!fs::exists(config), "matching user data left behind");
    require(fs::exists(otherConfig) && fs::exists(unrelated), "unrelated files were removed");

    auto valid = fixture.desktop("valid.desktop", "/unused");
    require(remove(fixture, {{"desktop", valid}, {"opt-dir", "/opt"}}) != 0,
            "unsafe /opt target accepted");
    require(fs::exists(valid), "validation failure caused partial deletion");

    auto outside = fixture.root / "outside/escape.desktop";
    write(outside, "keep");
    fs::create_directory_symlink(outside.parent_path(), fixture.data / "applications/escape");
    require(remove(fixture, {{"desktop", fixture.data / "applications/escape/escape.desktop"}}) != 0,
            "symlinked parent escaped application directory");
    require(fs::exists(outside), "outside launcher removed");
}

void optTests() {
    require(std::getenv("KDE_UNINSTALL_TEST_SANDBOX") && ::geteuid() == 0,
            "opt tests require the CTest bubblewrap sandbox");
    Fixture fixture;
    const fs::path root = "/opt/2026 测试软件";
    const auto executable = root / "runtime/start";
    write(executable, "#!/bin/sh\nexit 0\n", true);
    write(root / "resources/data", "bundle data");
    write("/opt/other-app/keep", "keep sibling app");
    fs::create_symlink(executable, "/usr/local/bin/opt-test");
    auto link = fixture.home / ".local/bin/opt-test-alias";
    fs::create_symlink("/usr/local/bin/opt-test", link);
    auto desktop = fixture.desktop("manual.desktop", "opt-test-alias %U");
    auto related = fixture.desktop("manual-alt.desktop", "bash -c '\"/opt/2026 测试软件/runtime/start\" %U'");
    write(root / "app.desktop", "[Desktop Entry]\nType=Application\nName=Internal\nExec=opt-test-alias\n");
    fs::create_symlink(root / "app.desktop", "/usr/share/applications/manual-linked.desktop");
    auto unrelated = fixture.desktop("unrelated.desktop", "/opt/other-app/run");

    auto plan = ku::removalPlanForDesktop(desktop);
    require(plan && plan->manager == "manual" && plan->packageName == root,
            "marker-free /opt bundle through chained links not recognized");
    require(hasTarget(*plan, "desktop", related), "shell launcher not associated");
    require(hasTarget(*plan, "desktop", "/usr/share/applications/manual-linked.desktop"),
            "symlinked desktop not associated");
    require(hasTarget(*plan, "command-link", link) && hasTarget(*plan, "command-link", "/usr/local/bin/opt-test"),
            "command link chain missing");
    require(!hasTarget(*plan, "desktop", unrelated), "unrelated /opt launcher selected");

    // Common wrappers must resolve the payload, never select the shell/env package.
    for (const auto &exec : {"env -i -u OLD DATA=/tmp \"/opt/2026 测试软件/runtime/start\" %f",
                             "/bin/sh \"/opt/2026 测试软件/runtime/start\"",
                             "bash -lc 'exec \"/opt/2026 测试软件/runtime/start\" %U'"}) {
        auto wrapped = ku::removalPlanForDesktop(fixture.desktop("wrapped.desktop", exec));
        require(wrapped && wrapped->packageName == root, "wrapper payload not recognized: " + std::string(exec));
    }
    auto relative = ku::removalPlanForDesktop(fixture.desktop("relative.desktop", "./runtime/start", "Path=" + root.string() + "\n"));
    require(relative && relative->packageName == root, "Path-relative executable not recognized");
    for (const auto &exec : {"sh -c 'echo test; opt-test-alias'", "env -u", "bash -c", "\"unclosed"}) {
        auto invalid = ku::removalPlanForDesktop(fixture.desktop("invalid.desktop", exec));
        require(invalid && invalid->manager == "启动器残留" &&
                    hasTarget(*invalid, "desktop", fixture.data / "applications/invalid.desktop") &&
                    std::none_of(invalid->removalTargets.begin(), invalid->removalTargets.end(),
                                 [](const auto &target) { return target.kind == "app-dir"; }),
                "ambiguous wrapper must fall back to launcher-only removal: " + std::string(exec));
    }

    auto vendor = fs::path("/opt/jetbrains/IDE 2026");
    write(vendor / "bin/run", "#!/bin/sh\n", true);
    auto vendorPlan = ku::removalPlanForDesktop(fixture.desktop("vendor.desktop", "\"" + (vendor / "bin/run").string() + "\""));
    require(vendorPlan && vendorPlan->packageName == vendor, "vendor application root inferred incorrectly");
    write("/opt/google/chrome/chrome", "#!/bin/sh\n", true);
    auto chrome = ku::removalPlanForDesktop(fixture.desktop("chrome.desktop", "/opt/google/chrome/chrome"));
    require(chrome && chrome->packageName == "/opt/google/chrome", "vendor container selected instead of app");

    write("/opt/bin/shared-app", "#!/bin/sh\n", true);
    auto shared = ku::removalPlanForDesktop(fixture.desktop("shared.desktop", "/opt/bin/shared-app"));
    require(shared && hasTarget(*shared, "file", "/opt/bin/shared-app") &&
                !hasTarget(*shared, "app-dir", "/opt/bin"),
            "shared /opt/bin must remove only the program file");
    write("/opt/standalone", "#!/bin/sh\n", true);
    auto standalone = ku::removalPlanForDesktop(fixture.desktop("standalone.desktop", "/opt/standalone"));
    require(standalone && hasTarget(*standalone, "file", "/opt/standalone") &&
                !hasTarget(*standalone, "app-dir", "/opt"),
            "loose /opt executable must remove only the file");
    require(remove(fixture, {{"opt-dir", "/opt/jetbrains"}}) != 0, "vendor container removal accepted");
    require(remove(fixture, {{"command-link", link}}) != 0, "unassociated command link removal accepted");

    // Simulate an owned executable below a directory that has no package owner.
    write(fixture.tools / "rpm", "#!/bin/sh\ncase \"$*\" in\n*runtime/start*) echo fixture-package; exit 0;;\nesac\nexit 1\n", true);
    auto native = ku::removalPlanForDesktop(desktop);
    require(native && native->manager == "rpm" && native->removalTargets.empty(),
            "package-owned executable fell through to recursive removal");
    fs::remove(fixture.tools / "rpm");

    require(removePlan(fixture, *plan) == 0, "manual /opt removal failed");
    require(!fs::exists(root) && !fs::exists(related) && !fs::is_symlink(link) &&
                !fs::is_symlink("/usr/local/bin/opt-test") &&
                !fs::is_symlink("/usr/share/applications/manual-linked.desktop"),
            "manual bundle or linked launchers left behind");
    require(fs::exists(unrelated) && fs::exists("/opt/other-app/keep") && fs::exists(vendor),
            "sibling application removed");
}


void homeInstallTests() {
    Fixture fixture;

    // Archive unpacked into Downloads, launched via bin/ layout.
    auto bundle = fixture.home / "Downloads/Fooapp-1.2.3";
    write(bundle / "bin/fooapp", "#!/bin/sh\n", true);
    write(bundle / "lib/libfooapp.so", "lib");
    write(fixture.home / "Downloads/keep.txt", "keep");
    write(fixture.home / ".config/fooapp/fooapp.conf", "cfg");
    write(fixture.home / ".cache/Fooapp/blob", "cache");
    write(fixture.home / ".config/fooappx/keep", "keep");
    write(fixture.home / ".config/JetBrains/Fooapp2026.1/options", "vendor product data");
    write(fixture.home / ".config/JetBrains/Other2026.1/options", "keep");
    auto desktop = fixture.desktop("foo.desktop", "\"" + (bundle / "bin/fooapp").string() + "\" %F");
    auto plan = ku::removalPlanForDesktop(desktop);
    require(plan && plan->manager == "manual" && plan->packageName == bundle, "home bundle not recognized");
    require(hasTarget(*plan, "app-dir", bundle), "home bundle directory missing");
    require(hasTarget(*plan, "data-dir", fixture.home / ".config/fooapp") &&
                hasTarget(*plan, "data-dir", fixture.home / ".cache/Fooapp"),
            "user data for home bundle missing");
    require(!hasTarget(*plan, "data-dir", fixture.home / ".config/fooappx"), "prefix-only data match selected");
    require(hasTarget(*plan, "data-dir", fixture.home / ".config/JetBrains/Fooapp2026.1") &&
                !hasTarget(*plan, "data-dir", fixture.home / ".config/JetBrains"),
            "vendor product data matched incorrectly");
    require(remove(fixture, {{"data-dir", fixture.home / ".config/JetBrains"}}) != 0, "vendor data dir accepted");
    require(removePlan(fixture, *plan) == 0, "home bundle removal failed");
    require(!fs::exists(bundle) && !fs::exists(desktop) && fs::exists(fixture.home / "Downloads/keep.txt") &&
                fs::exists(fixture.home / ".config/fooappx/keep") &&
                fs::exists(fixture.home / ".config/JetBrains/Other2026.1/options"),
            "home bundle removal wrong");

    // AppImage outside ~/Applications.
    auto image = fixture.home / "Downloads/Bar-2.0-x86_64.AppImage";
    write(image, "elf", true);
    auto barPlan = ku::removalPlanForDesktop(fixture.desktop("bar.desktop", image.string()));
    require(barPlan && barPlan->manager == "AppImage" && hasTarget(*barPlan, "appimage", image),
            "AppImage in Downloads not recognized");

    // Loose script in ~/.local/bin removes only the file.
    auto script = fixture.home / ".local/bin/baz-tool";
    write(script, "#!/bin/sh\n", true);
    auto bazPlan = ku::removalPlanForDesktop(fixture.desktop("baz.desktop", "baz-tool"));
    require(bazPlan && hasTarget(*bazPlan, "file", script), "loose script not selected");
    require(std::none_of(bazPlan->removalTargets.begin(), bazPlan->removalTargets.end(),
                         [](const auto &t) { return t.kind == "app-dir"; }),
            "~/.local/bin treated as app directory");

    // Interpreter launch: the jar's directory is the app, never the runtime.
    auto jarApp = fixture.home / "apps/Qux";
    write(jarApp / "qux.jar", "jar");
    write(fixture.tools / "java", "#!/bin/sh\n", true);
    write(fixture.tools / "rpm", "#!/bin/sh\ncase \"$*\" in\n*tools/java*) echo java-runtime; exit 0;;\nesac\nexit 1\n", true);
    auto quxPlan = ku::removalPlanForDesktop(fixture.desktop("qux.desktop", "java -jar " + (jarApp / "qux.jar").string()));
    require(quxPlan && quxPlan->manager == "manual" && hasTarget(*quxPlan, "app-dir", jarApp),
            "java payload not recognized or runtime package selected");
    fs::remove(fixture.tools / "rpm");

    // Orphan launcher: program gone, launcher still removable.
    auto orphan = fixture.desktop("gone.desktop", "/nonexistent/gone-app");
    auto orphanPlan = ku::removalPlanForDesktop(orphan);
    require(orphanPlan && orphanPlan->manager == "启动器残留" && hasTarget(*orphanPlan, "desktop", orphan),
            "orphan launcher not removable");
    require(removePlan(fixture, *orphanPlan) == 0 && !fs::exists(orphan), "orphan launcher removal failed");

    // Browser web app: launcher only, browser untouched.
    write(fixture.tools / "chromium", "#!/bin/sh\n", true);
    auto web = fixture.desktop("chrome-abc-Default.desktop", "chromium --profile-directory=Default --app-id=abc");
    auto webPlan = ku::removalPlanForDesktop(web);
    require(webPlan && webPlan->manager == "网页应用" && hasTarget(*webPlan, "desktop", web) &&
                !hasTarget(*webPlan, "file", fixture.tools / "chromium"),
            "web app not handled");

    // Steam, Wine and flatpak launchers go to their own uninstallers.
    auto steam = ku::removalPlanForDesktop(fixture.desktop("game.desktop", "steam steam://rungameid/42"));
    require(steam && steam->manager == "steam" && steam->command.back() == "steam://uninstall/42",
            "steam game not handled");
    auto wine = ku::removalPlanForDesktop(fixture.desktop(
        "winapp.desktop", "env WINEPREFIX=\"/home/u/.wine\" wine C:\\\\\\\\x.lnk"));
    require(wine && wine->manager == "wine" && wine->command.back() == "uninstaller" &&
                wine->command[1] == "WINEPREFIX=/home/u/.wine",
            "wine launcher not handled");
    auto flatpak = ku::removalPlanForDesktop(fixture.desktop("fp.desktop", "/usr/bin/flatpak run --branch=stable org.example.App %U"));
    require(flatpak && flatpak->manager == "flatpak" && flatpak->packageName == "org.example.App",
            "flatpak run launcher not handled");

    // Guarded remover rejects dangerous targets.
    require(remove(fixture, {{"app-dir", fixture.home}}) != 0, "home accepted as app dir");
    require(remove(fixture, {{"app-dir", fixture.home / "Downloads"}}) != 0, "Downloads accepted as app dir");
    fs::create_directories(fixture.home / ".ssh");
    require(remove(fixture, {{"data-dir", fixture.home / ".ssh"}}) != 0, "~/.ssh accepted as data");
    require(remove(fixture, {{"data-dir", fixture.data / "applications"}}) != 0, "applications accepted as data");
    write(fixture.home / ".bashrc", "keep");
    require(remove(fixture, {{"file", fixture.home / ".bashrc"}}) == 0 || fs::exists(fixture.home / ".bashrc"),
            "unexpected remover state");
}

} // namespace

int main(int argc, char **argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--opt-sandbox") {
            optTests();
        } else {
            appImageTests();
            homeInstallTests();
        }
        std::cout << "removal tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
