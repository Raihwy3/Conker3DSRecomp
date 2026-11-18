// Save states (issue #94): saves the game as it is right now to the current slot, loads it back,
// or picks a slot (1 to 9). A state is a file in the savestates folder beside the settings.
//
// How a recompiled game can be saved at all is told in the runtime (ultramodern/save_states.hpp): a
// state is taken at a safe point, when every game thread but the running one waits (and the running
// one is at a known call), and loaded at a safe point where every thread is where it was in the state.
// So a request only asks for a save or a load: it's done at the next such moment (within a frame or so),
// and the result is shown in the messages' corner / bottom screen.

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

#if !defined(CONKER_3DS)
#include <SDL.h>
#include <thread>
#else
namespace recomp3ds {
    void dsp_synth_reset();
}
#endif

#include "librecomp/game.hpp"
#include "librecomp/save_states.hpp"
#include "ultramodern/ultramodern.hpp"
#include "ultramodern/save_states.hpp"

#include "conker.hpp"

namespace {
    using clock = std::chrono::steady_clock;

    constexpr char file_magic[8] = { 'C', 'B', 'F', 'D', 'S', 'T', 'A', 'T' };
    constexpr uint32_t file_version = 1;
    constexpr int slot_count = 9;
    // How long a save or a load keeps trying for a moment it can be done at.
    constexpr auto give_up_after = std::chrono::seconds(5);

    enum class Request { None, Save, Load };

    std::atomic<Request> request = Request::None;
    std::atomic<int> current_slot = 1;
    // The request's, set before it's made (and read only by the safe point callback until it's done).
    int request_slot = 1;
    clock::time_point requested_at;
    std::vector<uint8_t> load_state;
    std::vector<uint8_t> load_progress;
    std::string load_version;
    bool load_same_build = true;
    bool logged_not_now = false;
    std::string last_failure;
    uint32_t attempts = 0;

    std::filesystem::path folder() {
        return recomp::get_config_path() / "savestates";
    }

    std::filesystem::path slot_path(int slot) {
        return folder() / ("slot" + std::to_string(slot) + ".state");
    }

    // --- The file ---

    std::mutex write_mutex;
    void write_state(int slot, std::vector<uint8_t> state, std::vector<uint8_t> progress) {
        auto do_write = [slot](const std::vector<uint8_t>& state_buf, const std::vector<uint8_t>& progress_buf) {
            std::lock_guard lock{write_mutex};
            std::error_code ec;
            std::filesystem::create_directories(folder(), ec);
            const std::filesystem::path path = slot_path(slot);
            std::filesystem::path temporary = path;
            temporary += ".new";
            {
                std::ofstream file(temporary, std::ios::binary | std::ios::trunc);
                auto put_u32 = [&](uint32_t v) { file.write(reinterpret_cast<const char*>(&v), sizeof(v)); };
                auto put_u64 = [&](uint64_t v) { file.write(reinterpret_cast<const char*>(&v), sizeof(v)); };
                auto put_blk = [&](const void* d, size_t sz) {
                    put_u32((uint32_t)sz);
                    if (sz > 0) {
                        file.write(reinterpret_cast<const char*>(d), (std::streamsize)sz);
                    }
                };
                file.write(file_magic, sizeof(file_magic));
                put_u32(file_version);
                const std::string version = recomp::get_project_version().to_string();
                put_blk(version.data(), version.size());
                put_u64((uint64_t)std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                put_blk(progress_buf.data(), progress_buf.size());
                put_blk(state_buf.data(), state_buf.size());
                if (!file.good()) {
                    conker::notices::show("Couldn't write the state to slot " + std::to_string(slot));
                    return;
                }
            }
            std::filesystem::remove(path, ec);
            std::filesystem::rename(temporary, path, ec);
            if (ec) {
                conker::notices::show("Couldn't write the state to slot " + std::to_string(slot));
                return;
            }
        };
#if defined(CONKER_3DS)
        do_write(state, progress);
#else
        std::thread([do_write, state = std::move(state), progress = std::move(progress)]() {
            do_write(state, progress);
        }).detach();
#endif
    }

    // Reads a slot's file into its parts. False, with why, if there's none or it isn't one.
    bool read_state(int slot, std::vector<uint8_t>& state, std::vector<uint8_t>& progress, std::string& version, std::string& error) {
        std::ifstream file(slot_path(slot), std::ios::binary);
        if (!file) {
            error = "Slot " + std::to_string(slot) + " is empty";
            return false;
        }
        auto get = [&](void* out, size_t size) {
            return (bool)file.read(reinterpret_cast<char*>(out), (std::streamsize)size);
        };
        auto get_block = [&](std::vector<uint8_t>& out, uint32_t max_size) {
            uint32_t size = 0;
            if (!get(&size, sizeof(size)) || size > max_size) {
                return false;
            }
            out.resize(size);
            return size == 0 || get(out.data(), size);
        };
        char magic[sizeof(file_magic)] = {};
        uint32_t version_number = 0;
        uint64_t taken = 0;
        std::vector<uint8_t> version_bytes;
        if (!get(magic, sizeof(magic)) || std::memcmp(magic, file_magic, sizeof(magic)) != 0 ||
            !get(&version_number, sizeof(version_number)) || version_number != file_version ||
            !get_block(version_bytes, 1024) || !get(&taken, sizeof(taken)) ||
            !get_block(progress, 1024 * 1024) || !get_block(state, 16 * 1024 * 1024)) {
            error = "Slot " + std::to_string(slot) + " isn't a state this version can load";
            return false;
        }
        version.assign(version_bytes.begin(), version_bytes.end());
        return true;
    }

    // --- The safe points ---

    void finish() {
        load_state.clear();
        load_state.shrink_to_fit();
        load_progress.clear();
        request = Request::None;
        ultramodern::save_states::set_safe_points_wanted(false);
    }

    // Whether the request has waited too long for a moment it can be done at.
    bool gave_up(const char* what) {
        (void)what;
        if (clock::now() - requested_at < give_up_after) {
            return false;
        }
        return true;
    }

    void on_safe_point(uint8_t* rdram) {
        const Request pending = request.load();
        if (pending == Request::None) {
            return;
        }
        attempts++;
        // A state is only taken (or loaded) with nothing on its way to the game; that's checked
        // again, with the timers held, as it's done.
        if (!ultramodern::save_states::is_quiet()) {
            last_failure = "the game was busy";
            if (gave_up(pending == Request::Save ? "save" : "load")) {
                conker::notices::show("Couldn't save now: the game stayed busy. Try again");
                finish();
            }
            return;
        }

        if (pending == Request::Save) {
            std::vector<uint8_t> state;
            std::string error;
            if (!recomp::save_states::capture(rdram, state, error)) {
                last_failure = error;
                if (gave_up("save")) {
                    conker::notices::show("Couldn't save now (" + error + "). Try again");
                    finish();
                }
                return;
            }
            write_state(request_slot, std::move(state), {});
            conker::notices::show("State saved to slot " + std::to_string(request_slot));
            finish();
            return;
        }

        std::string error;
#if defined(CONKER_3DS)
        recomp3ds::dsp_synth_reset();
#endif
        const recomp::save_states::LoadResult result = recomp::save_states::restore(rdram, load_state, error);
        switch (result) {
            case recomp::save_states::LoadResult::Loaded:
#if defined(CONKER_3DS)
                recomp3ds::dsp_synth_reset();
#endif
                conker::local_achievements::state_loaded();
                conker::notices::show("Loaded the state in slot " + std::to_string(request_slot));
                finish();
                break;
            case recomp::save_states::LoadResult::NotNow:
                logged_not_now = true;
                last_failure = error;
                if (gave_up("load")) {
                    if (!load_same_build) {
                        conker::notices::show("Couldn't load slot " + std::to_string(request_slot) +
                            ": saved by another version (" + load_version + ")");
                    }
                    else {
                        conker::notices::show("Couldn't load slot " + std::to_string(request_slot) +
                            " right now. Try again in a moment");
                    }
                    finish();
                }
                break;
            case recomp::save_states::LoadResult::Invalid:
                conker::notices::show("Couldn't load slot " + std::to_string(request_slot) + ": " + error);
                finish();
                break;
        }
    }

    // --- Requests ---

    void start(Request what) {
        request_slot = current_slot;
        requested_at = clock::now();
        attempts = 0;
        last_failure.clear();
        logged_not_now = false;
        request = what;
        ultramodern::save_states::set_safe_points_wanted(true);
    }

    void ask_to_save() {
        if (request.load() != Request::None) {
            return;
        }
        start(Request::Save);
    }

    void ask_to_load() {
        if (request.load() != Request::None) {
            return;
        }
        const int slot = current_slot;
        std::string error;
        if (!read_state(slot, load_state, load_progress, load_version, error)) {
            conker::notices::show(error);
            return;
        }
        load_same_build = recomp::save_states::same_build(load_state);
        start(Request::Load);
    }

#if !defined(CONKER_3DS)
    int SDLCALL watch_keys(void*, SDL_Event* event) {
        if (event->type != SDL_KEYDOWN || event->key.repeat != 0 || !ultramodern::is_game_started()) {
            return 0;
        }
        switch (event->key.keysym.sym) {
            case SDLK_F5:
                ask_to_save();
                break;
            case SDLK_F7:
                ask_to_load();
                break;
            case SDLK_F6:
                current_slot = current_slot % slot_count + 1;
                conker::notices::show("Save state slot " + std::to_string(current_slot.load()), true);
                break;
            default:
                break;
        }
        return 0;
    }
#endif
}

void conker::save_states::init() {
    ultramodern::save_states::set_safe_point_callback(on_safe_point);
#if !defined(CONKER_3DS)
    SDL_AddEventWatch(watch_keys, nullptr);
#endif
}

void conker::save_states::set_slot(int slot) {
    current_slot = std::clamp(slot, 1, slot_count);
}

int conker::save_states::get_slot() {
    return current_slot.load();
}

void conker::save_states::save() {
    if (!ultramodern::is_game_started()) {
        return;
    }
    ask_to_save();
}

void conker::save_states::load() {
    if (!ultramodern::is_game_started()) {
        return;
    }
    ask_to_load();
}
