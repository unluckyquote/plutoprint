cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

# The workflow passes the version so the firmware and the installer manifest always agree.
if(DEFINED ENV{PLUTO_VERSION})
  set(PROJECT_VER "$ENV{PLUTO_VERSION}")
endif()
project(print_server)
