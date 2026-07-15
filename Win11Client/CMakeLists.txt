cmake_minimum_required(VERSION 3.21)
project(Win11Client VERSION 0.1 LANGUAGES CXX)

# ---- MeetUp Win11 client ----------------------------------------------------
# Qt6/QML desktop client for the MeetUp conference server.
# Milestone M0: themed UI shell on mock data (no networking/media yet).
# See docs/ROADMAP.md for the full plan.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2 Network WebSockets)

qt_standard_project_setup(REQUIRES 6.5)

qt_add_executable(Win11Client
    src/main.cpp
    src/net/ApiClient.cpp  
    src/net/AuthController.cpp 
    src/net/RoomController.cpp
    src/net/SignalingClient.cpp
)

# The Theme and MockData QML files are exposed to every screen as singletons.
set_source_files_properties(qml/Theme.qml    PROPERTIES QT_QML_SINGLETON_TYPE TRUE)
set_source_files_properties(qml/MockData.qml PROPERTIES QT_QML_SINGLETON_TYPE TRUE)

qt_add_qml_module(Win11Client
    URI MeetUp
    VERSION 1.0
    QML_FILES
        qml/Main.qml
        qml/Theme.qml
        qml/MockData.qml
        qml/AppIcon.qml
        qml/AppButton.qml
        qml/IconButton.qml
        qml/Field.qml
        qml/AppInput.qml
        qml/Card.qml
        qml/Badge.qml
        qml/Divider.qml
        qml/Avatar.qml
        qml/LinkRow.qml
        qml/AppModal.qml
        qml/AuthScaffold.qml
        qml/VideoTile.qml
        qml/ChatMessage.qml
        qml/ControlDock.qml
        qml/LoginScreen.qml
        qml/RegisterScreen.qml
        qml/AnonLobbyScreen.qml
        qml/HomeScreen.qml
        qml/ConferenceScreen.qml
    RESOURCES
        resources/fonts/Unbounded.ttf
        resources/fonts/Manrope.ttf
        resources/fonts/SpaceGrotesk.ttf
        resources/icons/mic.svg
        resources/icons/mic-off.svg
        resources/icons/video.svg
        resources/icons/video-off.svg
        resources/icons/screen.svg
        resources/icons/phone-off.svg
        resources/icons/chat.svg
        resources/icons/users.svg
        resources/icons/settings.svg
        resources/icons/arrow-right.svg
        resources/icons/send.svg
        resources/icons/x.svg
        resources/icons/check.svg
        resources/icons/plus.svg
        resources/icons/sun.svg
        resources/icons/moon.svg
        resources/icons/copy.svg
        resources/icons/image.svg
        resources/icons/paperclip.svg
)

set_target_properties(Win11Client PROPERTIES
    WIN32_EXECUTABLE ON
    MACOSX_BUNDLE OFF
)

target_link_libraries(Win11Client PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Network
    Qt6::WebSockets
)
