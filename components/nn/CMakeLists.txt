cmake_minimum_required(VERSION 3.24)
project(nn VERSION 0.0.1)

set(src
    src/nn/allocator.c
    src/nn/endian.c
    src/nn/vector.c
    src/nn/string_set.c
    src/nn/log.c
    src/nn/llist.c
    src/nn/string_map.c
    src/nn/string_int_map.c
    src/nn/set.c
    src/nn/ip_address.c
    src/nn/string.c)

add_library(nn "${src}")
add_library(NabtoCommon::nn ALIAS nn)
target_include_directories(nn PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>" "$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>")

target_sources(nn PUBLIC
    FILE_SET public_headers
    TYPE HEADERS
    BASE_DIRS include
    FILES
        include/nn/allocator.h
        include/nn/endian.h
        include/nn/ip_address.h
        include/nn/llist.h
        include/nn/log.h
        include/nn/set.h
        include/nn/string.h
        include/nn/string_int_map.h
        include/nn/string_map.h
        include/nn/string_set.h
        include/nn/vector.h
)

# # install(TARGETS nn
# #         EXPORT nn
# #         FILE_SET public_headers)
# # install(EXPORT nn
# #         FILE nnTargets.cmake
# #         DESTINATION lib/cmake/
# #         NAMESPACE NabtoCommon)

# include(GNUInstallDirs)
# set(config_install_dir "${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME}")
# set(generated_dir "${CMAKE_CURRENT_BINARY_DIR}/generated")

# set(version_config "${generated_dir}/${PROJECT_NAME}ConfigVersion.cmake")
# set(project_config "${generated_dir}/${PROJECT_NAME}Config.cmake")
# set(TARGETS_EXPORT_NAME "${PROJECT_NAME}Targets")
# set(namespace "${PROJECT_NAME}::")

# include(CMakePackageConfigHelpers)

# install(
#     TARGETS nn
#     EXPORT "${TARGETS_EXPORT_NAME}"
#     LIBRARY DESTINATION "${CMAKE_INSTALL_LIBDIR}"
#     ARCHIVE DESTINATION "${CMAKE_INSTALL_LIBDIR}"
#     RUNTIME DESTINATION "${CMAKE_INSTALL_BINDIR}"
#     INCLUDES DESTINATION "${CMAKE_INSTALL_INCLUDEDIR}"
#     FILE_SET public_headers
# )



# write_basic_package_version_file(
#     "${version_config}" COMPATIBILITY SameMajorVersion
# )

# configure_package_config_file(
#     "cmake/Config.cmake.in"
#     "${project_config}"
#     INSTALL_DESTINATION "${config_install_dir}"
# )

# # Config
# #   * <prefix>/lib/cmake/Foo/FooConfig.cmake
# #   * <prefix>/lib/cmake/Foo/FooConfigVersion.cmake
# install(
#     FILES "${project_config}" "${version_config}"
#     DESTINATION "${config_install_dir}"
# )

# # Config
# #   * <prefix>/lib/cmake/Foo/FooTargets.cmake
# install(
#     EXPORT "${TARGETS_EXPORT_NAME}"
#     NAMESPACE "${namespace}"
#     DESTINATION "${config_install_dir}"
# )
