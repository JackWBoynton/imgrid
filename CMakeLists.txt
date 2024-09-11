cmake_minimum_required(VERSION 3.15)

project(imgrid)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

if(NOT DEFINED IMGRID_STANDALONE)
  if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(IMGRID_STANDALONE ON)
  else()
    set(IMGRID_STANDALONE OFF)
  endif()
endif()

# cmake options
option(IMGRID_EXAMPLES "Build examples" ${IMGRID_STANDALONE})

if(NOT DEFINED IMGRID_IMGUI_TARGET)
  find_package(imgui CONFIG)
  if(NOT imgui_FOUND)
    message(STATUS "imgui not found, using bundled version")
    set(IMGUI_BUNDLE_BUILD_OBJECT_LIBS ON)
    add_subdirectory(external/imgui)
    set(IMGRID_IMGUI_TARGET imgui_imgui)
    set(IMGUI_INCLUDE_DIRS ${CMAKE_CURRENT_SOURCE_DIR}/external/imgui)
  else()
    set(IMGRID_IMGUI_TARGET imgui::imgui)
  endif()
endif()

if(MSVC)
  add_compile_options(/WX)
  # replace existing /W to avoid warning
  if(CMAKE_CXX_FLAGS MATCHES "/W[0-4]")
    string(REGEX REPLACE "/W[0-4]" "/W4" CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS}")
  else()
    add_compile_options(/W4)
  endif()
else()
  add_compile_options(-Wall -Wextra -Wpedantic -Werror)
endif()

add_library(imgrid)
target_sources(imgrid PRIVATE imgrid.cpp imgrid.h imgrid_internal.h
                              imgrid_grid_engine.h imgrid_grid_engine.cpp)
target_include_directories(imgrid PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}
                                         ${IMGUI_INCLUDE_DIRS})
target_link_libraries(imgrid PUBLIC ${IMGRID_IMGUI_TARGET})

if(IMGRID_EXAMPLES)

  add_executable(grid example/main.cpp)
  target_link_libraries(grid imgrid)
  if(APPLE)
    target_link_libraries(grid "-framework OpenGL")
  elseif(MSVC)
    target_link_libraries(grid "opengl32")
  else()
    target_link_libraries(grid X11 Xext GL)
  endif()
endif()
