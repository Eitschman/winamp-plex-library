#pragma once
#include <windows.h>
#include "../src/plugin/wasabi_sdk.h"
#include <cstdlib>

// Der Mock verwendet dieselben Dispatch-Header und denselben Allocatorvertrag wie der Host.
struct MockMemory final : api_memmgr {
    unsigned allocations = 0;
    int fail_after = -1;
    void* allocate(size_t size) {
        if (fail_after == 0) return nullptr;
        if (fail_after > 0) --fail_after;
        auto* result = std::malloc(size); if (result) ++allocations; return result;
    }
    void release(void* pointer) { if (pointer) { --allocations; std::free(pointer); } }
    int _dispatch(int message, void* result, void** parameters, int) override {
        switch (message) {
            case API_MEMMGR_SYSMALLOC: cb(&MockMemory::allocate, result, parameters); return 1;
            case API_MEMMGR_SYSFREE: vcb(&MockMemory::release, result, parameters); return 1;
            default: return 0;
        }
    }
};
struct MockMemoryFactory final : waServiceFactory {
    MockMemory memory;
    bool enabled = true;
    unsigned releases = 0;
    void* get(int) { return enabled ? &memory : nullptr; }
    int release(void* pointer) { if (pointer != &memory) return 0; ++releases; return 1; }
    int _dispatch(int message, void* result, void** parameters, int) override {
        switch (message) {
            case WASERVICEFACTORY_GETINTERFACE: cb(&MockMemoryFactory::get, result, parameters); return 1;
            case WASERVICEFACTORY_RELEASEINTERFACE: cb(&MockMemoryFactory::release, result, parameters); return 1;
            default: return 0;
        }
    }
};
struct MockServices final : api_service {
    MockMemoryFactory memory;
    waServiceFactory* registered = nullptr;
    bool memory_available = true, reject_registration = false;
    unsigned registrations = 0, deregistrations = 0;
    int add(waServiceFactory* factory) { if (!reject_registration) { registered = factory; ++registrations; } return 0; }
    int remove(waServiceFactory* factory) { if (registered == factory) { registered = nullptr; ++deregistrations; } return 0; }
    waServiceFactory* find(GUID guid) {
        if (guid == memMgrApiServiceGuid) return memory_available ? &memory : nullptr;
        return registered && registered->getGuid() == guid ? registered : nullptr;
    }
    int _dispatch(int message, void* result, void** parameters, int) override {
        switch (message) {
            case API_SERVICE_SERVICE_REGISTER: cb(&MockServices::add, result, parameters); return 1;
            case API_SERVICE_SERVICE_DEREGISTER: cb(&MockServices::remove, result, parameters); return 1;
            case API_SERVICE_SERVICE_GETSERVICEBYGUID: cb(&MockServices::find, result, parameters); return 1;
            default: return 0;
        }
    }
};
