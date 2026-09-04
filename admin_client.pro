QT += core gui network widgets

qtHaveModule(webenginewidgets) {
    QT += webenginewidgets
    DEFINES += HAS_QT_WEBENGINE
} else {
    message(Qt WebEngineWidgets module not found; dashboard tab will use HTML preview fallback.)
}

CONFIG += c++17
CONFIG -= app_bundle

TARGET = admin_client
TEMPLATE = app

SOURCES += \
    src/admin_client/main.cpp

FORMS += \
    src/admin_client/admin_client.ui
