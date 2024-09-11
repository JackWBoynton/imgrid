cmake_minimum_required(VERSION 3.16)
# https://github.com/ocornut/imgui
project(imgui_imgui)

set(CMAKE_CXX_STANDARD 17)

if(IMGUI_BUNDLE_BUILD_OBJECT_LIBS)
  add_library(
    imgui_imgui OBJECT
    src/imgui.cpp
    src/imgui_demo.cpp
    src/imgui_draw.cpp
    src/imgui_tables.cpp
    src/imgui_widgets.cpp
    src/imgui_spectrum.cpp
    backends/imgui_impl_glfw.cpp
    backends/imgui_impl_opengl3.cpp
    misc/freetype/imgui_freetype.cpp)

  target_include_directories(imgui_imgui PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/inc
                                                ${CMAKE_CURRENT_SOURCE_DIR})

  target_compile_options(imgui_imgui PRIVATE -Wno-unknown-warning-option)

  find_package(glfw3 CONFIG REQUIRED)
  target_link_libraries(imgui_imgui PUBLIC glfw)

  if(NOT EMSCRIPTEN)
    find_package(Freetype REQUIRED)
  else()
    find_package(Freetype CONFIG)
    if(NOT Freetype_FOUND)
      if(NOT DEFINED CUSTOM_FREETYPE_DIR)
        message(
          FATAL_ERROR
            "Freetype not found. Please set INSTRONIMBUS_CUSTOM_FREETYPE_DIR to the directory containing the FreetypeConfig.cmake file."
        )
      else()
        set(FREETYPE_INCLUDE_DIRS "${CUSTOM_FREETYPE_DIR}/include")
        set(FREETYPE_LIBRARY_DIRS "${CUSTOM_FREETYPE_DIR}/lib")
      endif()
    endif()
  endif()
  message(STATUS "Freetype include dirs: ${FREETYPE_INCLUDE_DIRS}")
  message(STATUS "Freetype library dirs: ${FREETYPE_LIBRARY_DIRS}")

  target_include_directories(imgui_imgui PUBLIC ${FREETYPE_INCLUDE_DIRS})
  target_link_directories(imgui_imgui PUBLIC ${FREETYPE_LIBRARY_DIRS})
  target_link_libraries(imgui_imgui PUBLIC ${FREETYPE_LIBRARIES})
endif()

add_library(imgui_includes INTERFACE)
target_include_directories(
  imgui_includes INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/inc
                           ${CMAKE_CURRENT_SOURCE_DIRS})
