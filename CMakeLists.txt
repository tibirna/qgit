#
# see http://qt-project.org/doc/qt-5/cmake-manual.html

# see also http://www.kdab.com/using-cmake-with-qt-5/
# see also http://stackoverflow.com/questions/16245147/unable-to-include-a-ui-form-header-of-qt5-in-cmake
# see also http://www.qtcentre.org/wiki/index.php?title=Compiling_Qt4_apps_with_CMake
#
cmake_minimum_required(VERSION 3.10.2)

project(qgit)
include(GNUInstallDirs)

# As moc files are generated in the binary dir, tell CMake
# to always look for includes there:
set(CMAKE_INCLUDE_CURRENT_DIR ON)

# Instruct CMake to run moc automatically when needed.
set(CMAKE_AUTOMOC ON)

set(QT_MIN_VERSION 5.15.0)
find_package(Qt5 ${QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets)
add_compile_definitions(QT_DISABLE_DEPRECATED_UP_TO=0x050F00)

include_directories(
    ${CMAKE_SOURCE_DIR}/src
)

set(CPP_SOURCES
    src/annotate.cpp
    src/cache.cpp
    src/commitimpl.cpp
    src/common.cpp
    src/consoleimpl.cpp
    src/customactionimpl.cpp
    src/dataloader.cpp
    src/domain.cpp
    src/exceptionmanager.cpp
    src/filecontent.cpp
    src/FileHistory.cc
    src/filelist.cpp
    src/fileview.cpp
    src/git.cpp
    src/lanes.cpp
    src/listview.cpp
    src/inputdialog.cpp
    src/mainimpl.cpp
    src/myprocess.cpp
    src/namespace_def.cpp
    src/patchcontent.cpp
    src/patchview.cpp
    src/qgit.cpp
    src/rangeselectimpl.cpp
    src/revdesc.cpp
    src/revsview.cpp
    src/settingsimpl.cpp
    src/smartbrowse.cpp
    src/treeview.cpp
)

# UIS_HDRS will be used later in add_executable
qt5_wrap_ui(UIS_HDRS
    src/commit.ui
    src/console.ui
    src/customaction.ui
    src/fileview.ui
    src/help.ui
    src/mainview.ui
    src/patchview.ui
    src/rangeselect.ui
    src/revsview.ui
    src/settings.ui
)

# and finally an resource file
set(RESOURCE_FILES
    src/icons.qrc
)

# this command will generate rules that will run rcc on all files from SAMPLE_RCS
# in result SAMPLE_RC_SRCS variable will contain paths to files produced by rcc
qt5_add_resources(RC_SRCS ${RESOURCE_FILES})

add_executable(qgit ${CPP_SOURCES} ${UIS_HDRS} ${RC_SRCS})
target_link_libraries(qgit Qt5::Widgets)


install(TARGETS qgit DESTINATION bin)

if (UNIX)
    install(FILES src/resources/qgit.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/48x48/apps)
    install(FILES src/resources/qgit.svg DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
    install(FILES qgit.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)
    install(FILES qgit.appdata.xml DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/metainfo)
endif()

# kate: indent-width 4; replace-tabs on;

# notes:
# http://stackoverflow.com/questions/15054117/aligning-qgraphicsitems-to-a-grid-when-dragging-and-dropping
