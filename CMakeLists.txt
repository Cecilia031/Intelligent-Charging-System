cmake_minimum_required(VERSION 3.16)

project(IntelligentChargingSystem VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)

find_package(QT NAMES Qt6 Qt5 REQUIRED COMPONENTS Core Network Sql Widgets)
find_package(Qt${QT_VERSION_MAJOR} REQUIRED COMPONENTS Core Network Sql Widgets)
find_package(Qt${QT_VERSION_MAJOR} QUIET COMPONENTS WebEngineWidgets)

add_executable(charging_server
    src/server/main.cpp
)

target_link_libraries(charging_server
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Sql
)

add_executable(user_client
    src/user_client/main.cpp
    src/user_client/user_client.ui
)

add_executable(admin_client
    src/admin_client/main.cpp
    src/admin_client/admin_client.ui
)

target_link_libraries(user_client
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Widgets
)

target_link_libraries(admin_client
    PRIVATE
        Qt${QT_VERSION_MAJOR}::Core
        Qt${QT_VERSION_MAJOR}::Network
        Qt${QT_VERSION_MAJOR}::Widgets
)

if(TARGET Qt${QT_VERSION_MAJOR}::WebEngineWidgets)
    target_link_libraries(admin_client
        PRIVATE
            Qt${QT_VERSION_MAJOR}::WebEngineWidgets
    )
    target_compile_definitions(admin_client
        PRIVATE
            HAS_QT_WEBENGINE
    )
endif()

target_compile_definitions(charging_server
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

target_compile_definitions(user_client
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)

target_compile_definitions(admin_client
    PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
)
