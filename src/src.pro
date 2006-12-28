TEMPLATE = app
CONFIG += qt warn_on exceptions
QT += qt3support
#QT_QPROCESS_DEBUG = 1
INCLUDEPATH += ../src
MAKEFILE = qmake
QMAKE_CXXFLAGS_RELEASE += -g3 -O0 -Wno-non-virtual-dtor -Wno-long-long
QMAKE_CXXFLAGS_DEBUG += -g3 -O0 -Wno-non-virtual-dtor -Wno-long-long
DESTDIR = ../bin
TARGET = qgit
BUILD_DIR = ../build

target.path = ~/bin
unix:INSTALLS += target

UI_DIR = $$BUILD_DIR
MOC_DIR = $$BUILD_DIR
RCC_DIR = $$BUILD_DIR
OBJECTS_DIR = $$BUILD_DIR
RESOURCES += icons.qrc

FORMS += mainview.ui \
         settings.ui \
         help.ui \
         rangeselect.ui \
         commit.ui \
         console.ui \
         fileview.ui \
         customaction.ui \
         patchview.ui \
         revsview.ui 
HEADERS += mainimpl.h \
           git.h \
           cache.h \
           annotate.h \
           common.h \
           help.h \
           settingsimpl.h \
           rangeselectimpl.h \
           commitimpl.h \
           lanes.h \
           treeview.h \
           myprocess.h \
           dataloader.h \
           exceptionmanager.h \
           patchview.h \
           fileview.h \
           revsview.h \
           domain.h \
           filelist.h \
           revdesc.h \
           listview.h \
           filecontent.h \
           consoleimpl.h \
           customactionimpl.h 
SOURCES += qgit.cpp \
           mainimpl.cpp \
           git.cpp \
           cache.cpp \
           annotate.cpp \
           settingsimpl.cpp \
           rangeselectimpl.cpp \
           commitimpl.cpp \
           git_startup.cpp \
           lanes.cpp \
           treeview.cpp \
           myprocess.cpp \
           dataloader.cpp \
           exceptionmanager.cpp \
           patchview.cpp \
           fileview.cpp \
           revsview.cpp \
           domain.cpp \
           filelist.cpp \
           listview.cpp \
           revdesc.cpp \
           filecontent.cpp \
           consoleimpl.cpp \
           customactionimpl.cpp \
           namespace_def.cpp 
