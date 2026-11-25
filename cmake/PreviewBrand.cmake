# Only these pinned assets may be embedded in an explicitly selected local preview.
function(kadro_require_kit_file path expected_sha256)
  set(file "${KADRO_PREVIEW_KIT}/${path}")
  if(NOT EXISTS "${file}" OR IS_DIRECTORY "${file}")
    message(FATAL_ERROR "Private WANTED 1.0.0 kit is missing ${path}")
  endif()
  file(SHA256 "${file}" actual_sha256)
  if(NOT actual_sha256 STREQUAL expected_sha256)
    message(FATAL_ERROR "Private WANTED 1.0.0 kit checksum mismatch: ${path}")
  endif()
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${file}")
endfunction()

get_filename_component(KADRO_PREVIEW_KIT "${KADRO_PREVIEW_KIT}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
kadro_require_kit_file(assets/logo-source.svg "94d30acc647ac467aada73b0ad0c859e637d3b21be618cabeb359e380d80787e")
kadro_require_kit_file(assets/mark-black-128.png "8988bfd6bf2968108285ca78078925019891fac217820b0c1e41ef4914cc113a")
kadro_require_kit_file(assets/mark-white-128.png "b611ad91acab4145429933b31d9fbc60ccbbba1c64f4115eaadc9739cceabb56")
kadro_require_kit_file(native/tokens.json "7d174c4ed65041c5571a1cdf549ac425bb07104ff84c73c8ec500c19c2625b9b")

qt_add_resources(kadro_ui preview_brand PREFIX "/wanted-view/preview" BASE "${KADRO_PREVIEW_KIT}"
  FILES
    "${KADRO_PREVIEW_KIT}/assets/mark-black-128.png"
    "${KADRO_PREVIEW_KIT}/assets/mark-white-128.png"
    "${KADRO_PREVIEW_KIT}/native/tokens.json"
)
target_compile_definitions(kadro_ui PRIVATE KADRO_PRIVATE_PREVIEW_BRAND)
