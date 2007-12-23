# Only for Windows installation set correct directory
# of git exe files. Then uncomment following line
# GIT_EXEC_DIR = C:\path\to\git\installation\directory

# Uncomment if compile with gcc and family (minGw)
# CONFIG += HAVE_GCC

# Platform dependent stuff
win32 {
    TARGET = qgit
    target.path = $$GIT_EXEC_DIR
}

unix {
    TARGET = qgit
    target.path = ~/bin
}

# General stuff
TEMPLATE = app
CONFIG += qt console warn_on exceptions debug_and_release
INCLUDEPATH += ../src
MAKEFILE = qmake
RESOURCES += icons.qrc

HAVE_GCC {
	QMAKE_CXXFLAGS_RELEASE += -g3 -O2 -Wno-non-virtual-dtor -Wno-long-long -pedantic -Wconversion
	QMAKE_CXXFLAGS_DEBUG += -g3 -O0 -Wno-non-virtual-dtor -Wno-long-long -pedantic -Wconversion
}

INSTALLS += target

# Directories
DESTDIR = ../bin
BUILD_DIR = ../build
UI_DIR = $$BUILD_DIR
MOC_DIR = $$BUILD_DIR
RCC_DIR = $$BUILD_DIR
OBJECTS_DIR = $$BUILD_DIR

# project files
FORMS += commit.ui console.ui customaction.ui fileview.ui help.ui \
         mainview.ui patchview.ui rangeselect.ui revsview.ui settings.ui

HEADERS += annotate.h cache.h commitimpl.h common.h config.h consoleimpl.h \
           customactionimpl.h dataloader.h domain.h exceptionmanager.h \
           filecontent.h filelist.h fileview.h git.h help.h lanes.h \
           listview.h mainimpl.h myprocess.h patchcontent.h patchview.h \
           rangeselectimpl.h revdesc.h revsview.h settingsimpl.h \
           smartbrowse.h treeview.h

SOURCES += annotate.cpp cache.cpp commitimpl.cpp consoleimpl.cpp \
           customactionimpl.cpp dataloader.cpp domain.cpp exceptionmanager.cpp \
           filecontent.cpp filelist.cpp fileview.cpp git.cpp git_startup.cpp \
           lanes.cpp listview.cpp mainimpl.cpp myprocess.cpp namespace_def.cpp \
           patchcontent.cpp patchview.cpp qgit.cpp rangeselectimpl.cpp \
           revdesc.cpp revsview.cpp settingsimpl.cpp smartbrowse.cpp treeview.cpp

DISTFILES += helpgen.sh resources/* todo.txt
DISTFILES += ../exception_manager.txt resources/qgit.png ../COPYING ../README

# Here we generate a batch called start_qgit.bat used, under Windows only,
# to start qgit with proper PATH set.
#
# NOTE: qgit must be installed in git directory, among git exe files
# for this to work. If you install with 'make install' this is already
# done for you.
#
# Remember to set proper GIT_EXEC_DIR value at the beginning of this file
#
win32 { 
    !exists($${GIT_EXEC_DIR}/git.exe) {
        error("I cannot found git files, please set GIT_EXEC_DIR in 'src.pro' file")
    }
    QGIT_BAT = ../start_qgit.bat
    CUR_PATH = $$system(echo %PATH%)
    LINE_1 = $$quote(set PATH=$$CUR_PATH;$$GIT_EXEC_DIR;)
    LINE_2 = $$quote(set PATH=$$CUR_PATH;)

    qgit_launcher.commands =    @echo @echo OFF > $$QGIT_BAT
    qgit_launcher.commands += && @echo $$LINE_1 >> $$QGIT_BAT
    qgit_launcher.commands += && @echo $$TARGET >> $$QGIT_BAT
    qgit_launcher.commands += && @echo $$LINE_2 >> $$QGIT_BAT

    QMAKE_EXTRA_TARGETS += qgit_launcher
    PRE_TARGETDEPS += qgit_launcher
}
