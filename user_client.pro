QT += core gui network widgets

CONFIG += c++17
CONFIG -= app_bundle

TARGET = user_client
TEMPLATE = app

SOURCES += \
    src/user_client/main.cpp

FORMS += \
    src/user_client/user_client.ui
