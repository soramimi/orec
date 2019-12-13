TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += /usr/lib/llvm-6.0/include
LIBS += -L/usr/lib/llvm-6.0/lib `/usr/bin/llvm-config-6.0 --libs` -ldl

SOURCES += main.cpp \
    json.cpp

HEADERS += \
    json.h

