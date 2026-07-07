cmake_minimum_required(VERSION 3.16)

project(MeetUpServer LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# Qt6: WebSockets тянет за собой Network; Core — базовый модуль.
find_package(Qt6 REQUIRED COMPONENTS Core Network WebSockets)

add_executable(MeetUpServer
    main.cpp

    # core/ — модель времени выполнения: сессии, комнаты, реестр комнат
    core/ClientSession.h       core/ClientSession.cpp
    core/ConferenceRoom.h      core/ConferenceRoom.cpp
    core/RoomRegistry.h        core/RoomRegistry.cpp

    # network/ — входные точки: WebSocket-relay и HTTP-раздача статики
    network/ConferenceServer.h network/ConferenceServer.cpp
    network/HttpFileServer.h   network/HttpFileServer.cpp
)

# Инклуды пишутся от корня проекта: #include "core/ClientSession.h"
target_include_directories(MeetUpServer PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

target_link_libraries(MeetUpServer PRIVATE
    Qt6::Core
    Qt6::Network
    Qt6::WebSockets
)

# Путь к веб-клиенту по умолчанию — папка рядом с исходниками.
# Так сервер найдёт index.html, откуда бы его ни запустили.
target_compile_definitions(MeetUpServer PRIVATE
    WEB_ROOT_DEFAULT="${CMAKE_CURRENT_SOURCE_DIR}/web"
)
