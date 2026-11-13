QT += core gui widgets
CONFIG += c++17 qt warn_on
CONFIG -= app_bundle

TEMPLATE = app
TARGET = subtitler

LOCAL = $$PWD/../.deps/local
HB = $$LOCAL/usr/include/harfbuzz
FB = $$LOCAL/usr/include/fribidi

INCLUDEPATH += $$PWD $$LOCAL/usr/include $$LOCAL/usr/include/freetype2 \
               /usr/include/freetype2 \
               $$HB $$FB $$LOCAL/usr/include/aarch64-linux-gnu/qt6

HEADERS += \
    types.h \
    FontManager.h \
    TextShaper.h \
    SubtitleRenderer.h \
    ProjectStore.h \
    VideoExporter.h \
    MainWindow.h \
    PreviewWidget.h \
    ControlCharModel.h

SOURCES += \
    main.cpp \
    types.cpp \
    FontManager.cpp \
    TextShaper.cpp \
    SubtitleRenderer.cpp \
    ProjectStore.cpp \
    VideoExporter.cpp \
    MainWindow.cpp \
    PreviewWidget.cpp \
    ControlCharModel.cpp

LIBS += -L$$LOCAL/usr/lib/aarch64-linux-gnu -L$$LOCAL/lib/aarch64-linux-gnu
LIBS += -lharfbuzz -lfribidi -lfreetype
