# General stuff
TEMPLATE = app
CONFIG += qt warn_on exceptions
QT += qt3support
win32:DEFINES += ON_WINDOWS
INCLUDEPATH += ../src
MAKEFILE = qmake
QMAKE_CXXFLAGS_RELEASE += -g3 -O0 -Wno-non-virtual-dtor -Wno-long-long
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
target.path = ~/bin
unix:INSTALLS += target

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
