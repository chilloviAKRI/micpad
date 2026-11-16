QT += core gui qml quick quickcontrols2 dbus network multimedia
CONFIG += c++17 release link_pkgconfig
PKGCONFIG += libpipewire-0.3
TARGET = micpad
TEMPLATE = app
HEADERS += src/backend.h src/graph.h src/audioengine.h src/audiodecoder.h src/levels.h src/playbackqueue.h src/theme.h src/translator.h
SOURCES += src/main.cpp src/backend.cpp src/graph.cpp src/audioengine.cpp src/audiodecoder.cpp src/theme.cpp src/translator.cpp
RESOURCES += src/resources.qrc
