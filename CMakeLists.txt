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
find_package(Qt6 REQUIRED COMPONENTS Core DBus Widgets Multimedia MultimediaWidgets OpenGLWidgets)
find_package(OpenCV QUIET COMPONENTS core imgproc)

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
    src/gui/PaperCropProcessor.cpp
    src/gui/PaperCropProcessor.h
    src/gui/PaperCropSettings.h
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
    Qt6::DBus
    dev
)

target_compile_definitions(obsbot-gui PRIVATE
    $<$<CONFIG:Debug>:OBSBOT_DEBUG_GEOMETRY>
)

if(OpenCV_FOUND)
    target_compile_definitions(obsbot-gui PRIVATE OBSBOT_HAS_OPENCV=1)
    target_include_directories(obsbot-gui PRIVATE ${OpenCV_INCLUDE_DIRS})
    target_link_libraries(obsbot-gui PRIVATE ${OpenCV_LIBS})
endif()

# CLI Application
add_executable(obsbot-cli
    src/cli/meet2_test.cpp
    src/cli/CliOptions.cpp
    src/cli/CliOptions.h
    src/cli/GuiRemoteClient.cpp
    src/cli/GuiRemoteClient.h
    src/cli/CameraSelection.cpp
    src/cli/CameraSelection.h
    src/cli/PresetCommand.cpp
    src/cli/PresetCommand.h
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
    Qt6::Core
    Qt6::DBus
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
    add_executable(cli-options-test
        tests/CliOptionsTest.cpp
        src/cli/CliOptions.cpp
        src/cli/CliOptions.h
    )
    target_include_directories(cli-options-test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/cli
    )
    add_test(NAME cli-options COMMAND cli-options-test)

    add_executable(config-preset-validation-test
        tests/ConfigPresetValidationTest.cpp
        src/common/Config.cpp
        src/common/Config.h
    )
    target_include_directories(config-preset-validation-test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/common
    )
    add_test(NAME config-preset-validation COMMAND config-preset-validation-test)

    add_executable(paper-crop-processor-test
        tests/PaperCropProcessorTest.cpp
        src/gui/PaperCropProcessor.cpp
        src/gui/PaperCropProcessor.h
        src/gui/PaperCropSettings.h
    )
    target_include_directories(paper-crop-processor-test PRIVATE
        ${CMAKE_SOURCE_DIR}/src/gui
    )
    target_link_libraries(paper-crop-processor-test PRIVATE Qt6::Gui)
    if(OpenCV_FOUND)
        target_compile_definitions(paper-crop-processor-test PRIVATE OBSBOT_HAS_OPENCV=1)
        target_include_directories(paper-crop-processor-test PRIVATE ${OpenCV_INCLUDE_DIRS})
        target_link_libraries(paper-crop-processor-test PRIVATE ${OpenCV_LIBS})
    endif()
    add_test(NAME paper-crop-processor COMMAND paper-crop-processor-test)
endif()
