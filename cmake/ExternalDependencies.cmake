# ExternalDependencies.cmake
# Configures external library subdirectories


include(commonlibsse)
include(cef)

# SKSEHelpers (header-only): hooks, call pipelines, ResourceLock, PointerLinkedList, variant/trait utilities.
# Reuses the CommonLibSSE::CommonLibSSE target added by commonlibsse.cmake and brings in minhook::minhook.
set(SKSEHELPERS_DIR "${CMAKE_SOURCE_DIR}/external/SKSEHelpers")
add_subdirectory("${SKSEHELPERS_DIR}" "${CMAKE_BINARY_DIR}/external_builds/SKSEHelpers" EXCLUDE_FROM_ALL)

# Helper function to add external directories to a target
function(add_external_dependencies target_name)

    # Add target-specific include directories (PRIVATE to avoid pollution)
    target_include_directories(${target_name} PRIVATE
        ${CMAKE_SOURCE_DIR}/external/commonlibsse-ng/include
    )

    target_link_libraries(${target_name} PRIVATE SKSEHelpers::SKSEHelpers)
endfunction()
