# Conker's Bad Fur Day: Recompiled for the Nintendo 3DS.
cmake_minimum_required(VERSION 3.20)
project(ConkerRecomp3DS LANGUAGES C CXX ASM)

set(CONKER_ROOT "${CMAKE_CURRENT_LIST_DIR}/../..")
set(RT64_3DS_DIR "${CONKER_ROOT}/../renderer" CACHE PATH "rt64-3ds checkout")
set(RT64_3DS_NMR_DIR "${CONKER_ROOT}/tools/N64ModernRuntime")
# Conker's relocated .game (0x80810000..0x80A0A130) and .debugger (0x80A10000..0x80A14D58)
# live in RDRAM up to 0x80A14D58; start the recomp heap at 0x80B00000 (and cap at 0x80F00000).
set(RT64_3DS_HEAP_START 0x80B00000)
set(RT64_3DS_LINEAR_HEAP_MB 16)
set(RT64_3DS_STATIC_MODS OFF)

add_subdirectory("${RT64_3DS_DIR}" "${CMAKE_CURRENT_BINARY_DIR}/rt64-3ds")

file(GLOB FUNC_SOURCES "${CONKER_ROOT}/RecompiledFuncs/funcs_*.c")
add_library(RecompiledFuncs STATIC
    ${FUNC_SOURCES}
    "${CONKER_ROOT}/RecompiledFuncs/lookup.cpp"
)
target_include_directories(RecompiledFuncs PRIVATE
    "${CONKER_ROOT}/RecompiledFuncs"
    "${CMAKE_CURRENT_LIST_DIR}"
    "${RT64_3DS_DIR}/src/platform"
    "${RT64_3DS_DIR}/src/renderer"
    "${RT64_3DS_NMR_DIR}/N64Recomp/include"
    "${RT64_3DS_NMR_DIR}/ultramodern/include"
    "${RT64_3DS_NMR_DIR}/librecomp/include"
)
target_compile_options(RecompiledFuncs PRIVATE
    -O2 -fno-strict-aliasing -w
    -Wno-implicit-function-declaration -Wno-error=implicit-function-declaration
    -include "${CMAKE_CURRENT_LIST_DIR}/conker_host_protos.h"
)

add_executable(ConkerRecomp
    main_3ds.cpp
    updater/native_update.c
    updater/native_update_http.c
    updater/native_update_3ds.c
    conker_reticle.cpp
    conker_modifiers.cpp
    conker_tlb_pages.cpp
    "${CONKER_ROOT}/host/src/overlays.cpp"
    "${CONKER_ROOT}/host/src/ultra_extras.cpp"
    "${CONKER_ROOT}/host/src/mod_api.cpp"
    "${CONKER_ROOT}/host/src/widescreen.cpp"
    "${CONKER_ROOT}/host/src/cutscene_aspect.cpp"
    "${CONKER_ROOT}/host/src/field_of_view.cpp"
    "${CONKER_ROOT}/host/src/interpolation.cpp"
    "${CONKER_ROOT}/host/src/model_detail.cpp"
    "${CONKER_ROOT}/host/src/render_fixes.cpp"
    "${CONKER_ROOT}/host/src/look_aim.cpp"
    "${CONKER_ROOT}/host/src/mouse_camera.cpp"
    "${CONKER_ROOT}/host/src/local_achievements.cpp"
    "${CONKER_ROOT}/host/src/save_states.cpp"
    conker_audio_hle.cpp
    conker_mp3_native.cpp
    conker_mp3_bus.cpp
    "${RT64_3DS_DIR}/src/platform/audio_hle_diff.cpp"
    "${CONKER_ROOT}/RecompiledFuncs/rsp/audio_ucode.cpp"
)
target_include_directories(ConkerRecomp PRIVATE
    "${CMAKE_CURRENT_LIST_DIR}"
    "${CMAKE_CURRENT_LIST_DIR}/shim"
    "${CONKER_ROOT}/host/src"
    "${CONKER_ROOT}/RecompiledFuncs"
    "${RT64_3DS_DIR}/src/platform"
    "${RT64_3DS_DIR}/src/renderer"
    "${RT64_3DS_NMR_DIR}/N64Recomp/include"
    "${RT64_3DS_NMR_DIR}/ultramodern/include"
    "${RT64_3DS_NMR_DIR}/librecomp/include"
)
target_compile_definitions(ConkerRecomp PRIVATE CONKER_3DS)
target_compile_options(ConkerRecomp PRIVATE -O2 -Wno-unused-parameter)
set_source_files_properties("${CONKER_ROOT}/RecompiledFuncs/rsp/audio_ucode.cpp"
    PROPERTIES COMPILE_OPTIONS "-O3;-fno-strict-aliasing;-w")

target_link_libraries(ConkerRecomp PRIVATE
    recomp_3ds rt64_3ds RecompiledFuncs ultramodern librecomp citro3d ctru m
)
target_link_options(ConkerRecomp PRIVATE "-Wl,-Map,${CMAKE_CURRENT_BINARY_DIR}/ConkerRecomp.map")

option(CONKER_NULL_RENDERER "Build with rt64-3ds's null renderer" OFF)
if(CONKER_NULL_RENDERER)
    target_compile_definitions(ConkerRecomp PRIVATE CONKER_NULL_RENDERER)
endif()

set(CONKER_TITLE "Conker: 3DS Recomp")
set(CONKER_DESCRIPTION "A recompilation of Conker for Nintendo 3DS")
add_3dsx(ConkerRecomp
    TITLE "${CONKER_TITLE}"
    DESCRIPTION "${CONKER_DESCRIPTION}"
    ICON "${CMAKE_CURRENT_SOURCE_DIR}/icon.png"
)

# HTTPS/ZIP dependencies come from devkitPro portlibs, never from a ROM.
set(CONKER_UPDATE_PORTLIBS "${DEVKITPRO}/portlibs/3ds" CACHE PATH "3DS curl/mbedTLS/minizip/zlib prefix")
foreach(lib minizip curl mbedtls mbedx509 mbedcrypto z)
    if(NOT EXISTS "${CONKER_UPDATE_PORTLIBS}/lib/lib${lib}.a")
        message(FATAL_ERROR "Missing ${lib}: install 3ds-curl, 3ds-mbedtls and 3ds-zlib, or set CONKER_UPDATE_PORTLIBS")
    endif()
    target_link_libraries(ConkerRecomp PRIVATE "${CONKER_UPDATE_PORTLIBS}/lib/lib${lib}.a")
endforeach()
target_link_libraries(ConkerRecomp PRIVATE ctru m)
file(READ "${CONKER_ROOT}/../release.json" release_json)
string(JSON update_repo GET "${release_json}" repository)
string(JSON update_tag GET "${release_json}" tag)
if(NOT update_repo STREQUAL "" AND NOT update_repo MATCHES "^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")
    message(FATAL_ERROR "release.json repository must be owner/repo")
endif()
if(NOT update_tag MATCHES "^v[0-9]+\\.[0-9]+\\.[0-9]+(-r[0-9]+)?$")
    message(FATAL_ERROR "release.json tag must be vMAJOR.MINOR.PATCH or vMAJOR.MINOR.PATCH-rN")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CONKER_ROOT}/../release.json")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/update_config.h" "#define NATIVE_UPDATE_REPOSITORY \"${update_repo}\"\n#define NATIVE_RELEASE_TAG \"${update_tag}\"\n")
file(READ "${CMAKE_CURRENT_SOURCE_DIR}/updater/cacert.pem" ca)
string(REPLACE "\n" "\\n\"\n\"" ca "${ca}")
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/native_update_ca.h" "static const char native_update_ca[]=\n\"${ca}\";\n")
target_include_directories(ConkerRecomp PRIVATE updater "${CMAKE_CURRENT_BINARY_DIR}" "${CONKER_UPDATE_PORTLIBS}/include")
target_compile_definitions(ConkerRecomp PRIVATE CURL_STATICLIB)
