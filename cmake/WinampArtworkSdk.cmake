# Unveränderte Wasabi-/Agave-Header aus dem geprüften SDK-5.55-Archiv.
set(ARTWORK_SDK_HEADERS
  "Agave/AlbumArt/svc_albumArtProvider.h|dbd0b50118265f83225b9e3ecfd02832128c0cabec18261efcee0d069054986e"
  "Wasabi/bfc/dispatch.h|fbf6189a1e16bb7bb138174d7e3939286dbc21c3257a1a4654b18031b960147b"
  "Wasabi/bfc/std_mkncc.h|f72884c3a8f7aab51c8a4c63c08b9645db4f03f9b381adbc7c6f3a4436b0d8b7"
  "Wasabi/bfc/nsguid.h|14013d6a62e6f0215a20705991809e22d17e74912dd7466baf6d2a5f691705e2"
  "Wasabi/bfc/platform/types.h|f38d60c7fdcdd2547778ed124b5d36ad41f58bb8b72d0615306ec6f03c93420c"
  "Wasabi/bfc/platform/guid.h|db9585443659a5f57b7dd7f8ebbdfb04868a4df4276c03dfe49c011805a01f47"
  "Wasabi/bfc/platform/platform.h|918a496b410399e9fa770d746e76d9e6d8224ff27c69cdd88109f097a1c599c2"
  "Wasabi/bfc/platform/win32.h|2994363f85c28bf702eda6c5f23153c15906c6a65c836406ba7472d9e21ba5b8"
  "Wasabi/api/service/waservicefactory.h|6dcdbaee324432d10fc0c3ef9580e3ffc523e93f525cf7a1d39c74cbe64d94b0"
  "Wasabi/api/service/api_service.h|5ff84846c7621ab09ff2abbf556b3032b9770deecc38ef939dc197dd49ced693"
  "Wasabi/api/memmgr/api_memmgr.h|dff0fb105c79b8c7dc980b55ec8990ba8d6fd29899ad68ba3834df034abcb7db"
)
foreach(ARTWORK_SDK_ENTRY IN LISTS ARTWORK_SDK_HEADERS)
  string(REPLACE "|" ";" ARTWORK_SDK_PAIR "${ARTWORK_SDK_ENTRY}")
  list(GET ARTWORK_SDK_PAIR 0 ARTWORK_SDK_FILE)
  list(GET ARTWORK_SDK_PAIR 1 ARTWORK_SDK_HASH)
  if(NOT EXISTS "${WINAMP_SDK_ROOT}/${ARTWORK_SDK_FILE}")
    message(FATAL_ERROR "Album-Art-SDK-Header fehlt: ${ARTWORK_SDK_FILE}. SDK-Beschaffung gemäß docs/build.md ausführen.")
  endif()
  file(SHA256 "${WINAMP_SDK_ROOT}/${ARTWORK_SDK_FILE}" ARTWORK_SDK_ACTUAL)
  if(NOT ARTWORK_SDK_ACTUAL STREQUAL ARTWORK_SDK_HASH)
    message(FATAL_ERROR "Album-Art-SDK-Prüfsumme falsch: ${ARTWORK_SDK_FILE}")
  endif()
endforeach()
