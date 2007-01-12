# Only for Windows installation set correct directory
# of git exe files. Then uncomment following line
# GIT_EXEC_DIR = C:\path\to\git\installation\directory


# General stuff
TEMPLATE = app
CONFIG += qt warn_on exceptions debug_and_release
QT += qt3support
win32:DEFINES += ON_WINDOWS
INCLUDEPATH += ../src
MAKEFILE = qmake
QMAKE_CXXFLAGS_RELEASE += -g3 -O2 -Wno-non-virtual-dtor -frepo -Wno-long-long
QMAKE_CXXFLAGS_DEBUG += -g3 -O0 -Wno-non-virtual-dtor -Wno-long-long

# Directories
DESTDIR = ../bin
TARGET = qgit
BUILD_DIR = ../build
UI_DIR = $$BUILD_DIR
MOC_DIR = $$BUILD_DIR
RCC_DIR = $$BUILD_DIR
OBJECTS_DIR = $$BUILD_DIR

# misc
RESOURCES += icons.qrc
win32:target.path = $$GIT_EXEC_DIR
unix:target.path = ~/bin
INSTALLS += target

# project files
FORMS += commit.ui console.ui customaction.ui fileview.ui help.ui \
         mainview.ui patchview.ui rangeselect.ui revsview.ui settings.ui

HEADERS += annotate.h cache.h commitimpl.h common.h consoleimpl.h \
           customactionimpl.h dataloader.h domain.h exceptionmanager.h \
           filecontent.h filelist.h fileview.h git.h help.h lanes.h \
           listview.h mainimpl.h myprocess.h patchview.h rangeselectimpl.h \
           revdesc.h revsview.h settingsimpl.h treeview.h

SOURCES += annotate.cpp cache.cpp commitimpl.cpp consoleimpl.cpp \
           customactionimpl.cpp dataloader.cpp domain.cpp exceptionmanager.cpp \
           filecontent.cpp filelist.cpp fileview.cpp git.cpp git_startup.cpp \
           lanes.cpp listview.cpp mainimpl.cpp myprocess.cpp namespace_def.cpp \
           patchview.cpp qgit.cpp rangeselectimpl.cpp revdesc.cpp \
           revsview.cpp settingsimpl.cpp treeview.cpp



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

START_BAT = ..\start_qgit.bat
CUR_PATH = $$system(echo %PATH%)
TEXT = $$quote(set PATH=$$CUR_PATH;$$GIT_EXEC_DIR;)

run_qgit.commands  =    @echo @echo OFF > $$START_BAT
run_qgit.commands += && @echo $$TEXT   >> $$START_BAT
run_qgit.commands += && @echo $$TARGET >> $$START_BAT

QMAKE_EXTRA_TARGETS += run_qgit
PRE_TARGETDEPS += run_qgit
}
