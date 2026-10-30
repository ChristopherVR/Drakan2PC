# The registry is shared with ISO preparation and deployment. New discs need
# metadata, not another CMake branch or a runtime preprocessor definition.
set(DRAKAN_DISC_REGISTRY "${CMAKE_CURRENT_LIST_DIR}/../scripts/buildtool/regions.json")
get_filename_component(DRAKAN_DISC_ROOT "${CMAKE_CURRENT_LIST_DIR}/.." ABSOLUTE)
file(READ "${DRAKAN_DISC_REGISTRY}" DRAKAN_DISC_JSON)
string(JSON DRAKAN_DISC_COUNT LENGTH "${DRAKAN_DISC_JSON}" regions)
math(EXPR DRAKAN_DISC_LAST "${DRAKAN_DISC_COUNT} - 1")
set(DRAKAN_DISC_IDS "")
foreach(index RANGE ${DRAKAN_DISC_LAST})
    string(JSON id MEMBER "${DRAKAN_DISC_JSON}" regions ${index})
    list(APPEND DRAKAN_DISC_IDS "${id}")
endforeach()

function(drakan_configure_disc_profile target region)
    if(NOT region IN_LIST DRAKAN_DISC_IDS)
        message(FATAL_ERROR "Unknown disc profile '${region}'; choose from ${DRAKAN_DISC_IDS}")
    endif()
    find_package(Python3 3.11 REQUIRED COMPONENTS Interpreter)
    string(JSON profile GET "${DRAKAN_DISC_JSON}" regions "${region}" runtime_profile)
    set(generator "${DRAKAN_DISC_ROOT}/scripts/buildtool/generate_runtime_profile.py")
    set(output "${CMAKE_CURRENT_BINARY_DIR}/${target}-disc-profile")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
        "${DRAKAN_DISC_REGISTRY}" "${DRAKAN_DISC_ROOT}/${profile}" "${generator}")
    execute_process(COMMAND "${Python3_EXECUTABLE}" "${generator}"
        --registry "${DRAKAN_DISC_REGISTRY}" --root "${DRAKAN_DISC_ROOT}" --region "${region}"
        --output "${output}/drakan_disc_profile.generated.h"
        RESULT_VARIABLE result ERROR_VARIABLE error)
    if(NOT result EQUAL 0)
        message(FATAL_ERROR "Cannot generate ${region} runtime profile: ${error}")
    endif()
    target_include_directories(${target} PUBLIC "${output}")
endfunction()
