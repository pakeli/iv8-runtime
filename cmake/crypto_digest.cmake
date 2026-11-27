# Build the four unchanged upstream C files needed for BOM-10 digest.
# This module is included only for IV8_LINK_V8=ON.
include("${CMAKE_CURRENT_LIST_DIR}/crypto_pin.cmake")

set(IV8_CRYPTO_ARCHIVE
    "${CMAKE_SOURCE_DIR}/data/crypto/tf-psa-crypto-${IV8_PINNED_CRYPTO_VERSION}.tar.bz2"
    CACHE FILEPATH "Verified TF-PSA-Crypto source archive (can be pre-provisioned offline)")
option(IV8_CRYPTO_ALLOW_DOWNLOAD "Download the pinned crypto archive if missing" ON)

if(NOT EXISTS "${IV8_CRYPTO_ARCHIVE}")
  if(NOT IV8_CRYPTO_ALLOW_DOWNLOAD)
    message(FATAL_ERROR "Pinned crypto archive missing: ${IV8_CRYPTO_ARCHIVE}; pre-provision it or enable IV8_CRYPTO_ALLOW_DOWNLOAD")
  endif()
  get_filename_component(_iv8_crypto_archive_dir "${IV8_CRYPTO_ARCHIVE}" DIRECTORY)
  file(MAKE_DIRECTORY "${_iv8_crypto_archive_dir}")
  file(DOWNLOAD "${IV8_PINNED_CRYPTO_URL}" "${IV8_CRYPTO_ARCHIVE}.download"
      EXPECTED_HASH "SHA256=${IV8_PINNED_CRYPTO_SHA256}"
      TLS_VERIFY ON TIMEOUT 180 STATUS _iv8_crypto_download_status)
  list(GET _iv8_crypto_download_status 0 _iv8_crypto_download_code)
  if(NOT _iv8_crypto_download_code EQUAL 0)
    message(FATAL_ERROR "Unable to fetch pinned crypto archive: ${_iv8_crypto_download_status}")
  endif()
  file(RENAME "${IV8_CRYPTO_ARCHIVE}.download" "${IV8_CRYPTO_ARCHIVE}")
endif()

file(SHA256 "${IV8_CRYPTO_ARCHIVE}" _iv8_crypto_actual_sha256)
if(NOT _iv8_crypto_actual_sha256 STREQUAL IV8_PINNED_CRYPTO_SHA256)
  message(FATAL_ERROR "Pinned crypto archive SHA256 mismatch: ${IV8_CRYPTO_ARCHIVE}")
endif()

set(_iv8_crypto_unpack "${CMAKE_CURRENT_BINARY_DIR}/_deps/iv8-crypto-${IV8_PINNED_CRYPTO_SHA256}")
set(_iv8_crypto_source "${_iv8_crypto_unpack}/tf-psa-crypto-${IV8_PINNED_CRYPTO_VERSION}")
set(_iv8_crypto_stamp "${_iv8_crypto_unpack}/iv8-extracted.sha256")
if(NOT EXISTS "${_iv8_crypto_stamp}")
  file(MAKE_DIRECTORY "${_iv8_crypto_unpack}")
  execute_process(COMMAND "${CMAKE_COMMAND}" -E tar xjf "${IV8_CRYPTO_ARCHIVE}"
      WORKING_DIRECTORY "${_iv8_crypto_unpack}" RESULT_VARIABLE _iv8_crypto_extract_status)
  if(NOT _iv8_crypto_extract_status EQUAL 0)
    message(FATAL_ERROR "Unable to extract the verified crypto archive")
  endif()
  file(WRITE "${_iv8_crypto_stamp}" "${IV8_PINNED_CRYPTO_SHA256}\n")
endif()

enable_language(C)
add_library(iv8_crypto_digest STATIC
    "${_iv8_crypto_source}/drivers/builtin/src/sha1.c"
    "${_iv8_crypto_source}/drivers/builtin/src/sha256.c"
    "${_iv8_crypto_source}/drivers/builtin/src/sha512.c"
    "${_iv8_crypto_source}/platform/platform_util.c")
set_target_properties(iv8_crypto_digest PROPERTIES
    C_STANDARD 99 C_STANDARD_REQUIRED YES POSITION_INDEPENDENT_CODE ON
    C_VISIBILITY_PRESET hidden)
target_include_directories(iv8_crypto_digest PRIVATE
    "${_iv8_crypto_source}/include"
    "${_iv8_crypto_source}/drivers/builtin/include"
    "${_iv8_crypto_source}/core"
    "${CMAKE_CURRENT_LIST_DIR}")
target_compile_definitions(iv8_crypto_digest PRIVATE
    TF_PSA_CRYPTO_CONFIG_FILE="crypto_digest_config.h")

# The adapter is the only C++ file that needs the pinned upstream private API.
set_source_files_properties(src/core/browser/web/digest_backend.cpp PROPERTIES
    SKIP_PRECOMPILE_HEADERS ON
    INCLUDE_DIRECTORIES "${_iv8_crypto_source}/include;${_iv8_crypto_source}/drivers/builtin/include;${CMAKE_CURRENT_LIST_DIR}"
    COMPILE_DEFINITIONS "TF_PSA_CRYPTO_CONFIG_FILE=\"crypto_digest_config.h\";MBEDTLS_DECLARE_PRIVATE_IDENTIFIERS")
target_link_libraries(_core PRIVATE iv8_crypto_digest)

# Preserve the complete upstream dual-license notice; iv8 elects Apache-2.0.
install(FILES "${_iv8_crypto_source}/LICENSE"
    DESTINATION iv8/licenses/tf-psa-crypto-${IV8_PINNED_CRYPTO_VERSION})
install(FILES "${CMAKE_CURRENT_LIST_DIR}/crypto_pin.cmake"
    DESTINATION iv8/licenses/tf-psa-crypto-${IV8_PINNED_CRYPTO_VERSION})
message(STATUS "iv8: SHA backend TF-PSA-Crypto ${IV8_PINNED_CRYPTO_VERSION} (${IV8_PINNED_CRYPTO_SHA256})")
