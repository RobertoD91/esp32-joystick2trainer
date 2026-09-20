# ESP-IDF project file. PlatformIO drives the build (see platformio.ini); this file is
# what the IDF CMake build system needs to discover the main component in src/.
cmake_minimum_required(VERSION 3.16.0)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-joystick2trainer)
