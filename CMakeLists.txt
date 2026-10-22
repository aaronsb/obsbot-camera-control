cmake_minimum_required(VERSION 3.16)

project(obsbot-control)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

# Set output directories
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_DEBUG ${CMAKE_SOURCE_DIR}/bin)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY_RELEASE ${CMAKE_SOURCE_DIR}/bin)

# Find Qt6
find_package(Qt6 REQUIRED COMPONENTS Widgets Multimedia MultimediaWidgets OpenGLWidgets)

# SDK paths
set(SDK_INCLUDE_DIR ${CMAKE_SOURCE_DIR}/sdk/include)
set(SDK_LIB_DIR ${CMAKE_SOURCE_DIR}/sdk/lib)

# GUI Application
add_executable(obsbot-gui
    src/gui/main.cpp
    src/gui/MainWindow.cpp
    src/gui/MainWindow.h
    src/gui/CameraController.cpp
    src/gui/CameraController.h
    src/gui/TrackingControlWidget.cpp
    src/gui/TrackingControlWidget.h
    src/gui/PTZControlWidget.cpp
    src/gui/PTZControlWidget.h
    src/gui/CameraSettingsWidget.cpp
    src/gui/CameraSettingsWidget.h
    src/gui/FilterPreviewWidget.cpp
    src/gui/FilterPreviewWidget.h
    src/gui/CameraPreviewWidget.cpp
    src/gui/CameraPreviewWidget.h
    src/gui/VideoEffectsWidget.cpp
    src/gui/VideoEffectsWidget.h
    src/gui/VirtualCameraStreamer.cpp
    src/gui/VirtualCameraStreamer.h
    src/gui/PreviewWindow.cpp
    src/gui/PreviewWindow.h
    src/gui/V4l2Backend.cpp
    src/gui/V4l2Backend.h
    src/gui/XYPad.cpp
    src/gui/XYPad.h
    src/common/Config.cpp
    src/common/Config.h
    src/common/ShutterSpeed.h
    resources/resources.qrc
)

target_include_directories(obsbot-gui PRIVATE
    ${SDK_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_directories(obsbot-gui PRIVATE
    ${SDK_LIB_DIR}
)

target_link_libraries(obsbot-gui PRIVATE
    Qt6::Widgets
    Qt6::Multimedia
    Qt6::MultimediaWidgets
    Qt6::OpenGLWidgets
    dev
)

target_compile_definitions(obsbot-gui PRIVATE
    $<$<CONFIG:Debug>:OBSBOT_DEBUG_GEOMETRY>
)

# CLI Application
add_executable(obsbot-cli
    src/cli/meet2_test.cpp
    src/common/Config.cpp
    src/common/Config.h
)

target_include_directories(obsbot-cli PRIVATE
    ${SDK_INCLUDE_DIR}
    ${CMAKE_SOURCE_DIR}/src/common
)

target_link_directories(obsbot-cli PRIVATE
    ${SDK_LIB_DIR}
)

target_link_libraries(obsbot-cli PRIVATE
    dev
)

# Set RPATH for finding libdev.so
# Build uses local SDK, install uses system library path
set_target_properties(obsbot-gui PROPERTIES
    BUILD_RPATH "${SDK_LIB_DIR}"
    INSTALL_RPATH "/usr/lib"
)

set_target_properties(obsbot-cli PROPERTIES
    BUILD_RPATH "${SDK_LIB_DIR}"
    INSTALL_RPATH "/usr/lib"
)

include(CTest)
if(BUILD_TESTING)
    add_executable(autofocus-tests
        tests/AutofocusTest.cpp
        src/gui/TrackingControlWidget.cpp
        src/gui/CameraController.cpp
        src/gui/V4l2Backend.cpp
        src/gui/XYPad.cpp
        src/common/Config.cpp
    )
    target_include_directories(autofocus-tests PRIVATE
        ${SDK_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/src/gui
        ${CMAKE_SOURCE_DIR}/src/common
    )
    target_link_directories(autofocus-tests PRIVATE ${SDK_LIB_DIR})
    target_link_libraries(autofocus-tests PRIVATE Qt6::Widgets dev)
    set_target_properties(autofocus-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        BUILD_RPATH "${SDK_LIB_DIR}"
    )
    add_test(NAME autofocus COMMAND autofocus-tests)
    set_tests_properties(autofocus PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

    add_executable(exposure-tests
        tests/ExposureTest.cpp
        src/gui/CameraSettingsWidget.cpp
        src/gui/CameraController.cpp
        src/gui/V4l2Backend.cpp
        src/common/Config.cpp
    )
    target_include_directories(exposure-tests PRIVATE
        ${SDK_INCLUDE_DIR}
        ${CMAKE_SOURCE_DIR}/src/gui
        ${CMAKE_SOURCE_DIR}/src/common
    )
    target_link_directories(exposure-tests PRIVATE ${SDK_LIB_DIR})
    target_link_libraries(exposure-tests PRIVATE Qt6::Widgets dev)
    set_target_properties(exposure-tests PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}"
        BUILD_RPATH "${SDK_LIB_DIR}"
    )
    add_test(NAME exposure COMMAND exposure-tests)
    set_tests_properties(exposure PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()
