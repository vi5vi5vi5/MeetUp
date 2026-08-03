cmake_minimum_required(VERSION 3.21)
# RC — ради resources/app.rc: значок .exe (панель задач, Alt+Tab, ярлык).
# Без него CMake не знает, чем собирать .rc, и молча его пропустит.
# C — ради third_party/rnnoise: шумоподавление написано на чистом C.
project(Win11Client VERSION 1.07 LANGUAGES C CXX RC)

# ---- MeetUp Win11 client ----------------------------------------------------
# Qt6/QML desktop client for the MeetUp conference server.
# Milestone M0: themed UI shell on mock data (no networking/media yet).
# See docs/ROADMAP.md for the full plan.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

# MultimediaPrivate — отдельный компонент: цель Qt6::MultimediaPrivate сама по
# себе не появляется от Multimedia, её нужно запросить явно (см. ScreenSources).
# GuiPrivate — ради HMONITOR экрана: публичного способа узнать, какому монитору
# Windows соответствует QScreen, нет вовсе (см. src/ScreenSources.cpp).
find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui GuiPrivate Qml Quick QuickControls2
                                         Network WebSockets Multimedia MultimediaPrivate)

find_package(Opus CONFIG REQUIRED)

# FFmpeg: Find-модуль vcpkg должен победить одноимённый модуль самого Qt.
# Qt кладёт свой каталог модулей в CMAKE_MODULE_PATH при find_package(Qt6)
# выше, а его FindFFMPEG.cmake требует avformat, который мы осознанно не
# собираем, — и оставляет FFMPEG_LIBRARIES без swscale (LNK2019 на sws_*).
list(PREPEND CMAKE_MODULE_PATH "${VCPKG_INSTALLED_DIR}/${VCPKG_TARGET_TRIPLET}/share/ffmpeg")
find_package(FFMPEG REQUIRED)   # наполняет переменные FFMPEG_*

qt_standard_project_setup(REQUIRES 6.5)

# ---- RNNoise: шумоподавление микрофона -------------------------------------
# Вендорится, а не тянется из vcpkg: порт rnnoise там есть, но собран под
# autotools и в своих платформах Windows не заявляет вовсе. Зависимостей у
# библиотеки при этом нет ни одной — это чистый C, поэтому вендоринг здесь
# стоит дешевле, чем свой порт.
#
# Источник: github.com/xiph/rnnoise @ 70f1d256 (2025-02-22).
# Веса (src/rnnoise_data.c) в репозитории апстрима НЕ лежат — их качает
# download_model.sh по хэшу из model_version. Файл положен сюда уже скачанным:
# без него сборка не поднимется, а требовать sh-скрипт на Windows незачем.
set(RNNOISE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/third_party/rnnoise)
add_library(rnnoise STATIC
    ${RNNOISE_DIR}/src/denoise.c
    ${RNNOISE_DIR}/src/rnn.c
    ${RNNOISE_DIR}/src/pitch.c
    ${RNNOISE_DIR}/src/kiss_fft.c
    ${RNNOISE_DIR}/src/celt_lpc.c
    ${RNNOISE_DIR}/src/nnet.c
    ${RNNOISE_DIR}/src/nnet_default.c
    ${RNNOISE_DIR}/src/parse_lpcnet_weights.c
    ${RNNOISE_DIR}/src/rnnoise_data.c
    ${RNNOISE_DIR}/src/rnnoise_tables.c
)
target_include_directories(rnnoise PUBLIC ${RNNOISE_DIR}/include)
target_include_directories(rnnoise PRIVATE ${RNNOISE_DIR}/src)
# CMAKE_AUTOMOC включён глобально и цепляется к каждой цели. Здесь чистый C
# без единого Q_OBJECT, а один только файл весов — 74 МБ: пусть moc его не
# читает.
set_target_properties(rnnoise PROPERTIES AUTOMOC OFF)

# Векторизация выбирается на старте по фактическому процессору (RTCD), а не
# флагом сборки: собранный с /arch:AVX2 целиком бинарник просто не запустится
# там, где AVX2 нет. Замерено на 60 с звука: без SIMD 0.47 мс на кадр 10 мс,
# с диспетчером — 0.27 мс, то есть почти вдвое.
if(WIN32 AND CMAKE_SIZEOF_VOID_P EQUAL 8)
    target_sources(rnnoise PRIVATE
        ${RNNOISE_DIR}/src/x86/x86_dnn_map.c
        ${RNNOISE_DIR}/src/x86/x86cpu.c
        ${RNNOISE_DIR}/src/x86/nnet_sse4_1.c
        ${RNNOISE_DIR}/src/x86/nnet_avx2.c
    )
    target_compile_definitions(rnnoise PRIVATE RNN_ENABLE_X86_RTCD)
    # У MSVC нет ключа «включить SSE4.1»: на x64 эти интринсики доступны всегда,
    # а вот __SSE4_1__ компилятор сам не определяет — за него это делает
    # x86_arch_macros.h, но только увидев OPUS_X86_MAY_HAVE_SSE4_1.
    set_source_files_properties(${RNNOISE_DIR}/src/x86/nnet_sse4_1.c PROPERTIES
        COMPILE_DEFINITIONS OPUS_X86_MAY_HAVE_SSE4_1)
    # А вот AVX2 — ровно наоборот: нужен ключ, и строго на этот один файл.
    if(MSVC)
        set_source_files_properties(${RNNOISE_DIR}/src/x86/nnet_avx2.c PROPERTIES
            COMPILE_OPTIONS /arch:AVX2)
    else()
        set_source_files_properties(${RNNOISE_DIR}/src/x86/nnet_avx2.c PROPERTIES
            COMPILE_OPTIONS "-mavx2;-mfma")
    endif()
endif()

qt_add_executable(Win11Client
    src/main.cpp
    src/SysBridge.cpp
    src/HistoryStore.cpp
    src/MediaSettings.cpp
    src/ScreenSources.cpp
    src/GlobalHotkeys.cpp
    src/HotkeySpec.cpp
    src/net/ApiClient.cpp
    src/net/AuthController.cpp
    src/net/RoomController.cpp
    src/net/SignalingClient.cpp
    src/net/SignalingLink.cpp
    src/net/ChatImages.cpp
    src/net/ChatModel.cpp
    src/net/PersonalRoomController.cpp
    src/net/LinkController.cpp
    src/net/UpdateChecker.cpp
    src/Cli.cpp
    src/media/AudioEngine.cpp
    src/media/AudioWorker.cpp
    src/media/AutoGain.cpp
    src/media/Denoiser.cpp
    src/media/ScreenAudioCapture.cpp
    src/media/VideoDecoder.cpp
    src/media/VideoEncoder.cpp
    src/media/VideoSendWorker.cpp
    src/media/VideoRecvWorker.cpp
    src/media/ScreenCapturer.cpp
    src/media/VideoEngine.cpp
    src/media/MediaStats.cpp
    src/media/UiSounds.cpp
    src/crypto/E2eCipher.cpp
    src/crypto/E2eController.cpp
    # Значок приложения. В qrc он НЕ нужен: ресурс Windows и ресурс Qt — разные
    # механизмы, класть .ico в оба означало бы носить его дважды.
    resources/app.rc
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
        qml/BrandMark.qml
        qml/AppIcon.qml
        qml/AppButton.qml
        qml/IconButton.qml
        qml/Field.qml
        qml/AppInput.qml
        qml/AppToggle.qml
        qml/SoonChip.qml
        qml/SegmentedControl.qml
        qml/SettingSwitch.qml
        qml/SettingsCombo.qml
        qml/PercentSlider.qml
        qml/UpdatePill.qml
        qml/KeyBind.qml
        qml/Card.qml
        qml/Badge.qml
        qml/Divider.qml
        qml/Avatar.qml
        qml/LinkRow.qml
        qml/AppModal.qml
        qml/LinkBar.qml
        qml/AuthScaffold.qml
        qml/VideoTile.qml
        qml/LockPlate.qml
        qml/PeerVolumePopup.qml
        qml/ChatMessage.qml
        qml/ControlDock.qml
        qml/SettingsModal.qml
        qml/SettingsAudio.qml
        qml/SettingsVideo.qml
        qml/SettingsShare.qml
        qml/SettingsCrypto.qml
        qml/SettingsKeys.qml
        qml/SettingsInterface.qml
        qml/SettingsStats.qml
        qml/SettingsAbout.qml
        qml/ScreenPickerModal.qml
        qml/ScreenStage.qml
        qml/PinnedStage.qml
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
        resources/icons/link.svg
        resources/icons/image.svg
        resources/icons/paperclip.svg
        resources/icons/more.svg
        resources/icons/maximize.svg
        resources/icons/minimize.svg
        resources/icons/volume.svg
        resources/icons/volume-off.svg
        resources/icons/lock.svg
        resources/icons/keyboard.svg
        resources/icons/activity.svg
        resources/icons/layout.svg
        resources/icons/info.svg
        # Знак: две готовые заготовки под тему (см. BrandMark.qml).
        resources/brand/meetup-mark-light.svg
        resources/brand/meetup-mark-dark.svg
        # Звуки интерфейса: одно семейство из одного тембра, различаются числом
        # нот и направлением интервала (см. src/media/UiSounds.cpp). Все — 48 кГц
        # моно 16 бит без сжатия: QSoundEffect играет только такой PCM.
        resources/sounds/toggle-on.wav
        resources/sounds/toggle-off.wav
        resources/sounds/deafen-on.wav
        resources/sounds/deafen-off.wav
        resources/sounds/message.wav
        resources/sounds/peer-join.wav
        resources/sounds/peer-leave.wav
        resources/sounds/share-on.wav
        resources/sounds/share-off.wav
        resources/sounds/room-join.wav
)

# Версия одна на весь проект: CMake → QML (Qt.application.version в разделе
# «О программе»). Разъезжающиеся номера в интерфейсе и в сборке — классика.
target_compile_definitions(Win11Client PRIVATE APP_VERSION="${PROJECT_VERSION}")

# Откуда клиент берёт обновления: репозиторий GitHub в виде "владелец/имя".
# Вынесено в переменную, а не вшито в код, ровно ради форков: тот, кто соберёт
# свой клиент, должен обновляться из своего репозитория, а не из чужого.
# Пустое значение выключает проверку целиком (см. UpdateChecker::check).
set(UPDATE_REPO "vi5vi5vi5/MeetUp" CACHE STRING "GitHub repo for client updates (owner/name)")
target_compile_definitions(Win11Client PRIVATE UPDATE_REPO="${UPDATE_REPO}")

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
    Qt6::Multimedia
    # Приватный модуль — ради QCapturableWindowPrivate: публичного доступа к
    # хендлу перечисленного окна нет, а без него нельзя ни отсеять «призраки»
    # системы из списка, ни снять предпросмотр (см. src/ScreenSources.cpp).
    Qt6::MultimediaPrivate
    # …и ради QPlatformNativeInterface: HMONITOR у QScreen лежит только внутри
    # платформенного плагина, а без точного соответствия «экран Qt <-> монитор
    # Windows» на двух мониторах разъезжается курсор в демонстрации.
    Qt6::GuiPrivate
    Opus::opus
    rnnoise
)

if(WIN32)
    # dwmapi — проверка «окно скрыто композитором» (свёрнутые UWP-призраки);
    # user32/gdi32 — PrintWindow и DIB для миниатюр окон;
    # mmdevapi/ole32 — ActivateAudioInterfaceAsync и COM для захвата звука
    # приложений (process loopback, см. ScreenAudioCapture);
    # bcrypt — AES-256-GCM и PBKDF2 для сквозного шифрования (CNG есть в самой
    # Windows, поэтому OpenSSL в дистрибутив не едет, см. src/crypto);
    # windowsapp/d3d11/dxgi — Windows.Graphics.Capture: свой захват экрана и
    # окон (см. src/media/ScreenCapturer.cpp). Всё это части Windows SDK,
    # внешних зависимостей захват не добавляет вовсе.
    target_link_libraries(Win11Client PRIVATE dwmapi user32 gdi32 mmdevapi ole32 bcrypt
                                              windowsapp d3d11 dxgi)
endif()

# FFmpeg из vcpkg не даёт CMake-целей — только переменные Find-модуля.
target_include_directories(Win11Client PRIVATE ${FFMPEG_INCLUDE_DIRS})
target_link_directories(Win11Client PRIVATE ${FFMPEG_LIBRARY_DIRS})
target_link_libraries(Win11Client PRIVATE ${FFMPEG_LIBRARIES})
