TEMPLATE = app
CONFIG += console c++11
CONFIG -= app_bundle
CONFIG -= qt

INCLUDEPATH += /usr/lib/llvm-4.0/include
LIBS += -L/usr/lib/llvm-4.0/lib `llvm-config-4.0 --libs` -lpthread -lncurses -ldl

SOURCES += main.cpp \
    json.cpp

HEADERS += \
    json.h

