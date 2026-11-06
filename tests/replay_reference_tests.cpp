#include "plex/replay_references.h"
#include "plex/auth.h"
#include <fstream>
#include <iostream>
#include <thread>
#include <atomic>
#include <stdexcept>
#include <wincrypt.h>

namespace {
unsigned checks = 0;
void check(bool condition, const char* name) { ++checks; if (!condition) throw std::runtime_error(name); }
template<class Action> void rejects(Action action, const char* name) {
    bool rejected = false; try { action(); } catch (...) { rejected = true; } check(rejected, name);
}
void fixture(const std::filesystem::path& path, const nlohmann::json& document) {
    auto text = document.dump();
    DATA_BLOB plain{static_cast<DWORD>(text.size()), reinterpret_cast<BYTE*>(text.data())}, encrypted{};
    if (!CryptProtectData(&plain, L"Synthetic replay fixture", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &encrypted)) throw std::runtime_error("fixture_protect");
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(reinterpret_cast<const char*>(encrypted.pbData), encrypted.cbData); LocalFree(encrypted.pbData);
    if (!output) throw std::runtime_error("fixture_write");
}
}
int main() {
    const auto directory = std::filesystem::temp_directory_path() / (L"plex-replay-test-" + std::to_wstring(GetCurrentProcessId()));
    const auto path = directory / L"references.dpapi";
    const auto stop = CreateEventW(nullptr, TRUE, FALSE, nullptr);
    try {
        check(stop && !std::filesystem::exists(directory), "isolated_fixture");
        plex::ReplayReferences store(path);
        const plex::ReplayReference first{"http://127.0.0.1:12345/stream/" + std::string(48, 'a') + "/audio.mp3", "synthetic-server", "123", "/library/parts/456/1/file.mp3"};
        const auto second = plex::ReplayReference{(directory / L"playback" / (std::wstring(48, L'b') + L".flac")).u8string(), "synthetic-server", "124", "/library/parts/457/1/file.flac"};
        check(!store.find({first.alias})[0], "missing_store_empty");
        store.merge({first, second}, stop);
        const auto actual = store.find({second.alias, "unknown", first.alias});
        check(actual.size() == 3 && actual[0]->track == "124" && !actual[1] && actual[2]->part == first.part, "ordered_roundtrip");
        const plex::ReplayReferences restarted(path);
        check(restarted.find({first.alias})[0]->server == first.server, "new_instance_durable_reference");
        std::ifstream input(path, std::ios::binary); const std::string encrypted((std::istreambuf_iterator<char>(input)), {}); input.close();
        check(encrypted.find(first.alias) == encrypted.npos && encrypted.find(first.server) == encrypted.npos && encrypted.find(first.part) == encrypted.npos, "no_plaintext_identifiers");
        store.merge({first}, stop);
        check(store.find({first.alias})[0]->track == first.track, "idempotent_merge");
        auto conflict = first; conflict.track = "999";
        rejects([&] { store.merge({conflict}, stop); }, "alias_reassignment_denied");
        check(store.find({first.alias})[0]->track == "123", "collision_preserves_store");
        auto invalid = first; invalid.alias = "https://foreign.example/audio.mp3";
        rejects([&] { store.merge({invalid}, stop); }, "remote_alias_denied");
        invalid = first; invalid.alias += "?X-Plex-Token=synthetic";
        rejects([&] { store.merge({invalid}, stop); }, "query_credentials_denied");
        invalid = first; invalid.alias = "http://127.0.0.1:12345/stream/ordinary-radio.mp3";
        rejects([&] { store.merge({invalid}, stop); }, "foreign_loopback_stream_not_owned");
        invalid = second; invalid.alias = (directory / L"ordinary.flac").u8string();
        rejects([&] { store.merge({invalid}, stop); }, "foreign_local_file_not_owned");
        invalid = first; invalid.track = "../123";
        rejects([&] { store.merge({invalid}, stop); }, "track_path_injection_denied");
        invalid = first; invalid.part = "/library/metadata/123";
        rejects([&] { store.merge({invalid}, stop); }, "non_media_part_denied");
        SetEvent(stop);
        rejects([&] { store.merge({second}, stop); }, "cancelled_write_denied"); ResetEvent(stop);
        std::atomic<bool> failed{false};
        auto writer_record = first; writer_record.alias.replace(writer_record.alias.find("aaaa"), 1, "b"); writer_record.track = "125";
        std::thread writer([&] { try { plex::ReplayReferences(path).merge({writer_record}, stop); } catch (...) { failed = true; } });
        auto concurrent = first; concurrent.alias.replace(concurrent.alias.find("aaaa"), 1, "c"); concurrent.track = "126";
        store.merge({concurrent}, stop); writer.join();
        check(!failed && store.find({concurrent.alias, writer_record.alias})[0]->track == "126" && store.find({writer_record.alias})[0]->track == "125", "serialized_concurrent_writers");
        { std::ofstream broken(path, std::ios::binary | std::ios::trunc); broken << "invalid-protected-document"; }
        rejects([&] { store.find({first.alias}); }, "damaged_store_not_empty_fallback");
        rejects([&] { store.merge({first}, stop); }, "damaged_store_not_overwritten");
        fixture(path, {{"schema", 99}, {"references", nlohmann::json::array()}});
        rejects([&] { store.find({first.alias}); }, "unknown_protected_schema_denied");
        fixture(path, {{"schema", 1}, {"references", {{{"alias", first.alias}, {"server", first.server}, {"track", 123}, {"part", first.part}}}}});
        rejects([&] { store.find({first.alias}); }, "malformed_protected_identity_denied");
        nlohmann::json excess = nlohmann::json::array();
        for (unsigned index = 0; index < 100001; ++index) excess.push_back(nullptr);
        fixture(path, {{"schema", 1}, {"references", excess}});
        rejects([&] { store.find({first.alias}); }, "reference_count_limit_enforced");
        std::filesystem::remove(path); store.merge({first}, stop);
        check(store.find({first.alias})[0]->track == "123", "valid_countercheck_after_corruption");
        std::filesystem::remove_all(directory); CloseHandle(stop);
        std::cout << checks << " replay reference checks passed\n"; return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n'; if (stop) CloseHandle(stop); return 1;
    }
}
