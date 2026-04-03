cmake_minimum_required(VERSION 3.22)
project(qt_puppycam VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Network Gui)
find_package(ALSA REQUIRED)

add_executable(qt-puppycam
  src/main.cpp
  src/core/FrameHub.h
  src/core/FrameHub.cpp
  src/capture/V4L2MjpegGrabber.h
  src/capture/V4L2MjpegGrabber.cpp
  src/server/HttpServer.h
  src/server/HttpServer.cpp
  src/detection/MotionDetector.h
  src/detection/MotionDetector.cpp
  src/detection/AudioMonitor.h
  src/detection/AudioMonitor.cpp
  src/recording/ClipRecorder.h
  src/recording/ClipRecorder.cpp
  src/notify/TelegramNotifier.h
  src/notify/TelegramNotifier.cpp
)

target_link_libraries(qt-puppycam PRIVATE
  Qt6::Core
  Qt6::Network
  Qt6::Gui
  ALSA::ALSA
)

target_compile_options(qt-puppycam PRIVATE -Wall -Wextra -Wpedantic)
