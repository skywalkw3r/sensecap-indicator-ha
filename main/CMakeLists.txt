# Custom function to collect source files from a specified directory
function(collect_sources source_var)
  # Initialize the sources variable
  set(sources "")

  # Iterate over the directories to include
  foreach(dir IN LISTS ARGN)
    # Recursively glob all `.c` files in the directory
    file(GLOB_RECURSE dir_sources "${dir}/*.c")

    # Append the files to the sources variable
    list(APPEND sources ${dir_sources})
  endforeach()

  # Set the sources variable in the parent scope
  set(${source_var} ${sources} PARENT_SCOPE)
endfunction()

# The personal dashboard config is gitignored (real HA entity ids stay local);
# fresh checkouts and CI get a buildable copy of the template. Guarded so a
# reconfigure can never clobber an existing (user-edited) config.
if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/dashboard_config.h")
  configure_file("${CMAKE_CURRENT_LIST_DIR}/dashboard_config.example.h"
                 "${CMAKE_CURRENT_LIST_DIR}/dashboard_config.h" COPYONLY)
  message(STATUS "dashboard_config.h not found - created from dashboard_config.example.h (placeholder entities)")
endif()

# Define a list of directories to include
set(DIRECTORIES_TO_INCLUDE "util" "ui" "ha" "wifi" "sensor" "display" "settings" "rp2040" "btn" "mqtt" "storage" "cmd" "nav" "assets") # Recursively access folders | only modify here could be better

# Call the custom function to collect the source files
collect_sources(COLLECTED_SOURCES ${DIRECTORIES_TO_INCLUDE})

# Register the component
idf_component_register(
  SRCS "main.c" "lv_port.c" "indicator_model.c" "indicator_view.c" ${COLLECTED_SOURCES}
  INCLUDE_DIRS "." ${DIRECTORIES_TO_INCLUDE}
)

# Hold the application component (our own code) to -Wall -Wextra. The project no
# longer passes a blanket -w, but vendored (components/*) and managed components
# keep the compiler default warning set so upstream code is not flooded — only
# main/ is expected to be warning-free.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wall -Wextra)
