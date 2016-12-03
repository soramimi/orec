TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += /usr/lib/llvm-3.5/include
LIBS += -L/usr/lib/llvm-3.5/lib `llvm-config-3.5 --libs` -lpthread -lncurses -ldl

SOURCES += main.cpp \
    json.cpp

HEADERS += \
    json.h

