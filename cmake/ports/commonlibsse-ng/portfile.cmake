# CommonLibSSE-NG, maintained fork by alandtse.
# The upstream CharmedBaryon repo and the colorglass vcpkg registry this project
# used previously are both unmaintained (last touched in 2023) and only support
# Skyrim runtimes up to 1.6.640.
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO alandtse/CommonLibSSE-NG
    REF 70c1acd5261210982bd52f6d4468a082fe04d798  # v6.7.1
    SHA512 4b13f15f05c220f9754260086efa1688566ad8c60c9f8002110f342976d0dc653deffd39745b8398360092e21b83c40040c6a88d02aec2c1ee1eef98396e53a2
    HEAD_REF ng
)

# The openvr submodule is not part of the source archive, so fetch it separately.
# The commit is the one pinned by alandtse/CommonLibSSE-NG/extern/openvr at the REF above.
vcpkg_from_github(
    OUT_SOURCE_PATH SUBMODULE_PATH
    REPO ValveSoftware/openvr
    REF 60eb187801956ad277f1cae6680e3a410ee0873b
    SHA512 bb85b4705e7095ac65df9969112b2df8930cee7917cc5f14231c5a0ffeed7a73ffa60727fd32f8786a403656f95a3ec0f80bf3ceabc5b8ede964aefb920bc718
)

file(REMOVE_RECURSE "${SOURCE_PATH}/extern/openvr")
file(COPY "${SUBMODULE_PATH}/" DESTINATION "${SOURCE_PATH}/extern/openvr")

vcpkg_configure_cmake(
    SOURCE_PATH "${SOURCE_PATH}"
    PREFER_NINJA
    OPTIONS -DBUILD_TESTS=off -DSKSE_SUPPORT_XBYAK=on
)

vcpkg_install_cmake()
vcpkg_cmake_config_fixup(PACKAGE_NAME CommonLibSSE CONFIG_PATH lib/cmake)
vcpkg_copy_pdbs()

file(GLOB CMAKE_CONFIGS "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE/*.cmake")
file(INSTALL ${CMAKE_CONFIGS} DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")
file(INSTALL "${SOURCE_PATH}/cmake/CommonLibSSE.cmake" DESTINATION "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE")

# CommonLibSSEConfig.cmake only declares find_dependency(spdlog), but the target
# also links DirectXTK, so consumers need it resolved too.
set(CONFIG_FILE "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSEConfig.cmake")
file(READ "${CONFIG_FILE}" CONFIG_CONTENT)
file(WRITE "${CONFIG_FILE}"
    "include(CMakeFindDependencyMacro)\nfind_dependency(directxtk CONFIG)\n${CONFIG_CONTENT}"
)

file(INSTALL "${SOURCE_PATH}/extern/openvr/headers/" DESTINATION "${CURRENT_PACKAGES_DIR}/include")

# CommonLibSSE links openvr_api.lib by the absolute path it had in the build tree. vcpkg caches the
# installed package, so that path ends up pointing into whichever project built it first, and the
# link fails once that build tree is cleaned or deleted. Ship the library with the package and make
# the exported target refer to it there.
set(OPENVR_LIB "${SOURCE_PATH}/extern/openvr/lib/win64/openvr_api.lib")
file(INSTALL "${OPENVR_LIB}" DESTINATION "${CURRENT_PACKAGES_DIR}/lib")
file(INSTALL "${OPENVR_LIB}" DESTINATION "${CURRENT_PACKAGES_DIR}/debug/lib")
set(TARGETS_FILE "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE-targets.cmake")
file(READ "${TARGETS_FILE}" TARGETS_CONTENT)
string(FIND "${TARGETS_CONTENT}" "${OPENVR_LIB}" OPENVR_REFERENCE)
if(OPENVR_REFERENCE EQUAL -1)
    message(FATAL_ERROR "openvr_api.lib is no longer referenced by its build path: review this port.")
endif()
string(REPLACE "${OPENVR_LIB}" "\${_IMPORT_PREFIX}/lib/openvr_api.lib" TARGETS_CONTENT "${TARGETS_CONTENT}")
file(WRITE "${TARGETS_FILE}" "${TARGETS_CONTENT}")

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/share/CommonLibSSE/CommonLibSSE")

file(
    INSTALL "${SOURCE_PATH}/COPYING"
    DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}"
    RENAME copyright
)
