import qbs
import QbsFunctions

Product {
    type: "application"
    destinationDirectory: "./bin"
    targetName: "qgit"

    Depends { name: "cpp" }
    Depends { name: "Qt"; submodules: ["core"] }
    Depends {
        name: "Qt.gui";
        condition: Qt.core.versionMajor < 5
    }
    Depends {
        name: "Qt.widgets";
        condition: Qt.core.versionMajor >= 5
    }

    Probe {
        configure: {
            if (QbsFunctions.versionIsAtLeast(minimumQtVersion, Qt.core.version))
                throw Error("The minimum required version Qt: " + minimumQtVersion
                            + ". The current Qt version: " + Qt.core.version);
        }
    }

    cpp.cxxFlags: {
        var cxx = [
            "-std=c++11",
            "-Wno-non-virtual-dtor",
            "-Wno-long-long",
            "-pedantic",
        ];
        if (qbs.buildVariant === "debug")
            cxx.push("-ggdb3");
        else
            cxx.push("-s");

        if (project.conversionWarnEnabled)
            cxx.push("-Wconversion");

        return cxx;
    }

    cpp.includePaths: [
        "./",
    ]

    Group {
        name: "resources"
        files: {
            var files = [
                "icons.qrc"
            ];
            if (qbs.targetOS.contains('windows')
                && qbs.toolchain && qbs.toolchain.contains('msvc'))
                files.push("app_icon.rc");

            if (qbs.targetOS.contains('macos') &&
                qbs.toolchain && qbs.toolchain.contains('gcc'))
                files.push("resources/app_icon.rc");

            return files;
        }
    }

    Group {
        name: "windows"
        files: [
            "commitimpl.cpp",
            "commitimpl.h",
            "commit.ui",
            "consoleimpl.cpp",
            "consoleimpl.h",
            "console.ui",
            "customactionimpl.cpp",
            "customactionimpl.h",
            "customaction.ui",
            "fileview.cpp",
            "fileview.h",
            "fileview.ui",
            "help.h",
            "help.ui",
            "mainview.ui",
            "patchview.cpp",
            "patchview.h",
            "patchview.ui",
            "rangeselectimpl.cpp",
            "rangeselectimpl.h",
            "rangeselect.ui",
            "revsview.cpp",
            "revsview.h",
            "revsview.ui",
            "settingsimpl.cpp",
            "settingsimpl.h",
            "settings.ui",
        ]
    }

    Group {
        name: "others"
        files: [
            "../exception_manager.txt",
            "../README",
            "../README_WIN.txt",
            "../qgit_inno_setup.iss",
            "helpgen.sh",
            "todo.txt",
        ]
    }

    files: [
        "annotate.cpp",
        "annotate.h",
        "cache.cpp",
        "cache.h",
        "common.cpp",
        "common.h",
        "config.h",
        "dataloader.cpp",
        "dataloader.h",
        "domain.cpp",
        "domain.h",
        "exceptionmanager.cpp",
        "exceptionmanager.h",
        "filecontent.cpp",
        "filecontent.h",
        "filelist.cpp",
        "filelist.h",
        "git.cpp",
        "git.h",
        "inputdialog.cpp",
        "inputdialog.h",
        "lanes.cpp",
        "lanes.h",
        "listview.cpp",
        "listview.h",
        "mainimpl.cpp",
        "mainimpl.h",
        "myprocess.cpp",
        "myprocess.h",
        "patchcontent.cpp",
        "patchcontent.h",
        "revdesc.cpp",
        "revdesc.h",
        "smartbrowse.cpp",
        "smartbrowse.h",
        "treeview.cpp",
        "treeview.h",
        "FileHistory.cc",
        "FileHistory.h",
        "namespace_def.cpp",
        "qgit.cpp",
    ]

    //property var test: {
    //    console.info("=== Qt.core.version ===");
    //    console.info(Qt.core.version);
    //}
}
