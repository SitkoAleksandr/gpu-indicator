cmake_minimum_required(VERSION 3.20)
project(GpuIndicator VERSION 1.0.0 LANGUAGES CXX RC)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# Статический CRT: exe не требует установленного Visual C++ Redistributable.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

add_executable(GpuIndicator WIN32
  src/main.cpp
  src/icon.cpp
  src/nvml.cpp
  src/settings.cpp
  res/app.rc
)

target_include_directories(GpuIndicator PRIVATE src res)
target_compile_definitions(GpuIndicator PRIVATE
  UNICODE _UNICODE WIN32_LEAN_AND_MEAN NOMINMAX _WIN32_WINNT=0x0A00 WINVER=0x0A00
)
target_link_libraries(GpuIndicator PRIVATE shell32 user32 gdi32 advapi32)

if(MSVC)
  target_compile_options(GpuIndicator PRIVATE $<$<COMPILE_LANGUAGE:CXX>:/W4 /utf-8 /permissive->)
  # Манифест встраивается через app.rc.
  target_link_options(GpuIndicator PRIVATE /MANIFEST:NO)
else()
  target_compile_options(GpuIndicator PRIVATE
    $<$<COMPILE_LANGUAGE:CXX>:-Wall -Wextra -municode -ffunction-sections -fdata-sections>
  )
  target_link_options(GpuIndicator PRIVATE -municode -static -Wl,--gc-sections $<$<CONFIG:Release>:-s>)
endif()
