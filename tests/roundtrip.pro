QT += core
CONFIG += c++17 qt console warn_on
CONFIG -= app_bundle

TARGET = test_roundtrip
TEMPLATE = app

LOCAL = /workspace/.deps/local
SRC = /workspace/src

INCLUDEPATH += $$SRC $$LOCAL/usr/include $$LOCAL/usr/include/freetype2 \
               /usr/include/freetype2 \
               $$LOCAL/usr/include/harfbuzz $$LOCAL/usr/include/fribidi \
               $$LOCAL/usr/include/aarch64-linux-gnu/qt6 \
               $$LOCAL/usr/include/aarch64-linux-gnu/qt6/QtCore \
               $$LOCAL/usr/include/aarch64-linux-gnu/qt6/QtGui \
               $$LOCAL/usr/include/aarch64-linux-gnu/qt6/QtWidgets

SOURCES += test_roundtrip.cpp \
           $$SRC/types.cpp $$SRC/FontManager.cpp $$SRC/TextShaper.cpp \
           $$SRC/SubtitleRenderer.cpp $$SRC/ProjectStore.cpp

LIBS += -L$$LOCAL/usr/lib/aarch64-linux-gnu -L$$LOCAL/lib/aarch64-linux-gnu
LIBS += -lharfbuzz -lfribidi -lfreetype
