# The following lines of boilerplate have to be in your project's
# CMakeLists in this exact order for cmake to work correctly
cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET esp32s3)
set(PROJECT_VER "v1.1.0")

set(EXTRA_COMPONENT_DIRS components)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)

add_compile_options(-fdiagnostics-color=always)

project(indicator_ha)

# Warning policy:
#   * The "main" component (our application code) is compiled with -Wall -Wextra
#     and is expected to be warning-free (see main/CMakeLists.txt).
#   * Every other component is upstream code we do not own: the ESP-IDF
#     framework, managed components (LVGL, esp_lvgl_port) and the vendored
#     third-party drivers under components/. These were previously silenced by a
#     blanket project-wide -w. Re-apply -w to those components only so their
#     warnings are not inflicted on this build, while our own sources stay clean.
# (A blanket -w cannot be selectively re-enabled per component: -w overrides a
#  later -Wall, so it must simply be absent from the main component instead.)
idf_build_get_property(_indicator_build_components BUILD_COMPONENTS)
foreach(_indicator_comp IN LISTS _indicator_build_components)
    if(_indicator_comp STREQUAL "main")
        continue()
    endif()
    idf_component_get_property(_indicator_lib "${_indicator_comp}" COMPONENT_LIB)
    if(_indicator_lib AND TARGET ${_indicator_lib})
        get_target_property(_indicator_lib_type ${_indicator_lib} TYPE)
        if(NOT _indicator_lib_type STREQUAL "INTERFACE_LIBRARY")
            target_compile_options(${_indicator_lib} PRIVATE -w)
        endif()
    endif()
endforeach()
