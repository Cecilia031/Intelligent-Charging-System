QT += core network sql
QT -= gui

CONFIG += console c++17
CONFIG -= app_bundle

TARGET = charging_server
TEMPLATE = app

SOURCES += \
    src/server/main.cpp
