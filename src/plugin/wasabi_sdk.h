#pragma once
#include <cstdint>
// SDK 5.55 definiert int8_t als MSVC-__int8 (char). C++17 verwendet signed char.
// Nur der private SDK-Typname wird getrennt; Headerdateien und Dispatch-ABI bleiben unverändert.
#pragma push_macro("int8_t")
#define int8_t winamp_sdk_int8_t
#include <Agave/AlbumArt/svc_albumArtProvider.h>
#include <api/memmgr/api_memmgr.h>
#include <api/service/waservicefactory.h>
#pragma pop_macro("int8_t")
