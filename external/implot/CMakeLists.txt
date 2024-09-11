cmake_minimum_required(VERSION 3.16)
# https://github.com/epezent/implot
project(imgui_implot)

set(CMAKE_CXX_STANDARD 17)

if(IMGUI_BUNDLE_BUILD_OBJECT_LIBS)
  # HACK: removed implot_demo.cpp from here so I can include it directly and use
  # the functions from it
  add_library(imgui_implot OBJECT implot.cpp implot_items.cpp)

  target_include_directories(imgui_implot PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})

  target_link_libraries(imgui_implot PRIVATE imgui_includes)
endif()

target_include_directories(imgui_implot INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
