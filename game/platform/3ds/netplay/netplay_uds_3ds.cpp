#include "netplay_uds_3ds.h"
#include "netplay_lockstep.h"
#include "netplay_packets.h"

#include "conker_launcher.h"
#include "recomp3ds.h"
#include "rt64_3ds.h"
#include "ultramodern/input.hpp"
#include "ultramodern/timeline.hpp"

#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace {

using namespace conker_netplay;

constexpr u32 kWlanCommId = 0x0E0CBF00u; // Matches Conker 3DS Title ID low bits
constexpr u8 kWlanId8 = 0x01;
constexpr u8 kDataChannel = 1;
constexpr size_t kUdsSharedMemSize = 0x8000;
constexpr const char kPassphrase[] = "Conker3DSUDS";
constexpr u64 kResendIntervalMs = 20;
constexpr u64 kPeerTimeoutMs = 8000;

struct Session {
    bool uds_inited = false;
    bool hosting = false;
    bool connected = false;
    bool match_active = false;
    udsBindContext bind{};
    bool bound = false;

    uint64_t rom_hash = 0;
    int local_port = 0;
    int player_count = 1;
    int input_delay = kDefaultInputDelay;
    u16 port_node_id[kMaxPlayers]{};
    uint32_t port_client_id[kMaxPlayers]{};
    char names[kMaxPlayers][kMaxNameLen]{};
    uint8_t occupied_mask = 0;
    char local_name[kMaxNameLen]{};

    LockstepEngine engine{};
    u64 last_peer_ms[kMaxPlayers]{};
};

Session g_sess;

void load_local_username(char out[kMaxNameLen]) {
    std::memset(out, 0, kMaxNameLen);
    udsNodeInfo info{};
    if (R_SUCCEEDED(udsGenerateNodeInfo(&info, nullptr))) {
        char tmp[16]{};
        if (R_SUCCEEDED(udsGetNodeInfoUsername(&info, tmp)) && tmp[0] != '\0') {
            std::snprintf(out, kMaxNameLen, "%s", tmp);
            return;
        }
    }
    std::snprintf(out, kMaxNameLen, "3DS");
}

void draw_lobby(PrintConsole* top, PrintConsole* bottom,
                const char* stage, const char* msg, const char* detail, const char* actions,
                const char* const labels[3] = nullptr, const char* const notes[3] = nullptr,
                const int avail[3] = nullptr, int selected = -1, int menu = 0) {
    ConkerLauncherView view{};
    view.stage = stage;
    view.message = msg;
    view.detail = detail;
    view.actions = actions;
    view.menu = (menu == 0 && !labels) ? 3 : menu;
    view.selected = selected;
    if (labels) {
        for (int i = 0; i < 3; ++i) {
            view.labels[i] = labels[i];
            view.notes[i] = notes ? notes[i] : "";
            view.available[i] = avail ? avail[i] : 1;
        }
    }
    if (conker_launcher_draw(&view)) return;
    consoleSelect(top);
    consoleClear();
    std::printf("\x1b[3;3HCONKER 3DS - LOCAL WIRELESS\n\n   %s\n\n   %s\n\n   %s",
                stage ? stage : "", msg ? msg : "", detail ? detail : "");
    consoleSelect(bottom);
    consoleClear();
    if (menu && labels) {
        for (int i = 0; i < 3; ++i) {
            std::printf("\x1b[%d;3H%s %s", 8 + i * 3, selected == i ? ">" : " ", labels[i] ? labels[i] : "");
        }
    }
    std::printf("\x1b[22;3H%s", actions ? actions : "");
    gfxFlushBuffers();
    gfxSwapBuffers();
}

void drain_buttons() {
    while (aptMainLoop()) {
        hidScanInput();
        if (!(hidKeysHeld() & (KEY_A | KEY_B | KEY_X | KEY_Y | KEY_START | KEY_TOUCH))) break;
        gspWaitForVBlank();
    }
}

void send_packet(u16 dst_node, const uint8_t* buf, size_t len) {
    if (!g_sess.connected || len == 0) return;
    const Result rc = udsSendTo(dst_node, kDataChannel, UDS_SENDFLAG_Default, buf, len);
    if (UDS_CHECK_SENDTO_FATALERROR(rc)) {
        std::fprintf(stderr, "netplay: udsSendTo(%04x) failed: %08lx\n", dst_node, (unsigned long)rc);
    }
}

void cleanup_network() {
    if (g_sess.bound) {
        udsUnbind(&g_sess.bind);
        g_sess.bound = false;
    }
    if (g_sess.connected) {
        if (g_sess.hosting) udsDestroyNetwork();
        else udsDisconnectNetwork();
        g_sess.connected = false;
    }
    if (g_sess.uds_inited) {
        udsExit();
        g_sess.uds_inited = false;
    }
    g_sess.hosting = false;
    g_sess.match_active = false;
    g_sess.engine.stop();
}

void broadcast_lobby_state(uint8_t started = 0) {
    LobbyStatePacket pkt{};
    pkt.player_count = (uint8_t)g_sess.player_count;
    pkt.input_delay = (uint8_t)g_sess.input_delay;
    pkt.occupied_mask = g_sess.occupied_mask;
    pkt.started = started;
    for (int i = 0; i < kMaxPlayers; ++i) {
        pkt.client_ids[i] = g_sess.port_client_id[i];
        std::memcpy(pkt.names[i], g_sess.names[i], kMaxNameLen);
    }
    uint8_t raw[kMaxPacketBytes];
    const size_t n = encode_lobby_state(pkt, raw, sizeof(raw));
    send_packet(UDS_BROADCAST_NETWORKNODEID, raw, n);
}

void poll_game_packets() {
    uint8_t raw[kMaxPacketBytes];
    for (int iter = 0; iter < 32; ++iter) {
        size_t actual = 0;
        u16 src_node = 0;
        const Result rc = udsPullPacket(&g_sess.bind, raw, sizeof(raw), &actual, &src_node);
        if (R_FAILED(rc) || actual == 0) break;

        PacketKind kind{};
        if (!peek_packet_kind(raw, actual, kind)) continue;

        if (kind == PacketKind::Inputs) {
            InputsPacket pkt{};
            if (decode_inputs(raw, actual, pkt)) {
                g_sess.engine.on_remote_inputs(pkt);
                if (pkt.port < kMaxPlayers) {
                    g_sess.last_peer_ms[pkt.port] = osGetTime();
                }
                // Host relays client inputs to all peers so 3-4 player star topology works over UDS.
                if (g_sess.hosting && pkt.port != 0) {
                    send_packet(UDS_BROADCAST_NETWORKNODEID, raw, actual);
                }
            }
        } else if (kind == PacketKind::Leave) {
            LeavePacket pkt{};
            if (decode_leave(raw, actual, pkt)) {
                g_sess.engine.mark_peer_left(pkt.port);
                if (g_sess.hosting && pkt.port != 0) {
                    send_packet(UDS_BROADCAST_NETWORKNODEID, raw, actual);
                }
                std::fprintf(stderr, "netplay: player %d left match\n", (int)pkt.port + 1);
            }
        }
    }
}

void leave_to_local_control(const char* reason) {
    g_sess.match_active = false;
    g_sess.engine.stop();
    ultramodern::input::set_read_data_callback(nullptr);
    ultramodern::timeline::disable();
    rt64_3ds::set_focus_rect_query(nullptr);
    recomp3ds::screen_conker_set_subnote(reason ? reason : "Host disconnected - local control restored");
    std::fprintf(stderr, "netplay: %s\n", reason ? reason : "host disconnected");
}

void netplay_read_data_callback(ultramodern::input::Pad* pads, int max_controllers) {
    if (!g_sess.match_active || !g_sess.engine.active()) return;

    // Local hardware input always arrives on controller 0 from input_hid.cpp.
    Pad local_pad{};
    local_pad.button = pads[0].button;
    local_pad.stick_x = pads[0].stick_x;
    local_pad.stick_y = pads[0].stick_y;
    local_pad.err_no = 0;

    const InputsPacket out_pkt = g_sess.engine.push_local_input(local_pad);
    uint8_t raw[kMaxPacketBytes];
    const size_t raw_len = encode_inputs(out_pkt, raw, sizeof(raw));
    const u16 dst = g_sess.hosting ? UDS_BROADCAST_NETWORKNODEID : UDS_HOST_NETWORKNODEID;
    send_packet(dst, raw, raw_len);

    u64 wait_start = osGetTime();
    u64 last_resend = wait_start;

    while (!g_sess.engine.ready_to_advance()) {
        poll_game_packets();
        if (g_sess.engine.ready_to_advance()) break;

        const u64 now = osGetTime();
        if (now - last_resend >= kResendIntervalMs) {
            const InputsPacket retry = g_sess.engine.build_local_packet();
            const size_t rlen = encode_inputs(retry, raw, sizeof(raw));
            send_packet(dst, raw, rlen);
            last_resend = now;
        }
        if (now - wait_start >= kPeerTimeoutMs) {
            const uint8_t missing = g_sess.engine.missing_mask();
            for (int p = 0; p < g_sess.player_count; ++p) {
                if (missing & (1u << p)) {
                    std::fprintf(stderr, "netplay: player %d timed out at read %lu\n",
                                 p + 1, (unsigned long)g_sess.engine.current_read());
                    g_sess.engine.mark_peer_left(p);
                }
            }
            break;
        }
        svcSleepThread(500 * 1000ll); // 0.5 ms
    }

    // If the host (Player 1) disconnected, hand full local control back to this 3DS
    // so Player 2 isn't trapped in a match with no menu control.
    if (!g_sess.hosting && !(g_sess.engine.active_mask() & 1u)) {
        leave_to_local_control("Host disconnected - local control restored");
        return;
    }

    Pad synced[kMaxPlayers]{};
    if (g_sess.engine.pop_frame(synced)) {
        const int count = std::min(max_controllers, kMaxPlayers);
        for (int i = 0; i < count; ++i) {
            pads[i].button = synced[i].button;
            pads[i].stick_x = synced[i].stick_x;
            pads[i].stick_y = synced[i].stick_y;
            pads[i].err_no = synced[i].err_no;
        }
    }
}

bool run_host_lobby(PrintConsole* top, PrintConsole* bottom) {
    udsNetworkStruct net{};
    udsGenerateDefaultNetworkStruct(&net, kWlanCommId, kWlanId8, kMaxPlayers);
    const Result rc = udsCreateNetwork(&net, kPassphrase, sizeof(kPassphrase),
                                       &g_sess.bind, kDataChannel, UDS_DEFAULT_RECVBUFSIZE);
    if (R_FAILED(rc)) {
        char err[96];
        std::snprintf(err, sizeof(err), "udsCreateNetwork failed (0x%08lx). Check Wi-Fi switch.", (unsigned long)rc);
        draw_lobby(top, bottom, "LOCAL WIRELESS ERROR", err, "MAKE SURE WIRELESS IS ON", "B: BACK");
        drain_buttons();
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & (KEY_A | KEY_B)) break;
            gspWaitForVBlank();
        }
        return false;
    }

    g_sess.hosting = true;
    g_sess.connected = true;
    g_sess.bound = true;
    g_sess.local_port = 0;
    g_sess.player_count = 1;
    g_sess.occupied_mask = 1;
    std::memset(g_sess.names, 0, sizeof(g_sess.names));
    std::memset(g_sess.port_node_id, 0, sizeof(g_sess.port_node_id));
    std::memset(g_sess.port_client_id, 0, sizeof(g_sess.port_client_id));
    std::snprintf(g_sess.names[0], kMaxNameLen, "%s", g_sess.local_name);
    g_sess.port_node_id[0] = UDS_HOST_NETWORKNODEID;
    g_sess.port_client_id[0] = 1u;

    u64 last_bcast = 0;
    bool redraw = true;
    drain_buttons();

    while (aptMainLoop()) {
        hidScanInput();
        const u32 down = hidKeysDown();
        if (down & KEY_B) {
            return false;
        }
        if ((down & KEY_A) && g_sess.player_count >= 2) {
            StartMatchPacket spkt{};
            spkt.player_count = (uint8_t)g_sess.player_count;
            spkt.input_delay = (uint8_t)g_sess.input_delay;
            spkt.occupied_mask = g_sess.occupied_mask;
            uint8_t raw[kMaxPacketBytes];
            const size_t n = encode_start_match(spkt, raw, sizeof(raw));
            for (int r = 0; r < 6; ++r) {
                send_packet(UDS_BROADCAST_NETWORKNODEID, raw, n);
                broadcast_lobby_state(1);
                gspWaitForVBlank();
            }
            g_sess.match_active = true;
            return true;
        }

        uint8_t raw[kMaxPacketBytes];
        size_t actual = 0;
        u16 src_node = 0;
        while (R_SUCCEEDED(udsPullPacket(&g_sess.bind, raw, sizeof(raw), &actual, &src_node)) && actual > 0) {
            PacketKind kind{};
            if (!peek_packet_kind(raw, actual, kind)) continue;
            if (kind == PacketKind::Hello) {
                HelloPacket hello{};
                if (!decode_hello(raw, actual, hello)) continue;
                int slot = -1;
                for (int i = 1; i < kMaxPlayers; ++i) {
                    if ((hello.client_id != 0 && g_sess.port_client_id[i] == hello.client_id) ||
                        (src_node != 0 && g_sess.port_node_id[i] == src_node)) {
                        slot = i;
                        break;
                    }
                }
                const bool rom_ok = (hello.rom_hash == g_sess.rom_hash);
                if (slot < 0 && rom_ok) {
                    for (int i = 1; i < kMaxPlayers; ++i) {
                        if (!(g_sess.occupied_mask & (1u << i))) { slot = i; break; }
                    }
                }
                WelcomePacket wel{};
                wel.client_id = hello.client_id;
                if (slot >= 1 && rom_ok) {
                    g_sess.port_node_id[slot] = src_node;
                    g_sess.port_client_id[slot] = hello.client_id;
                    g_sess.occupied_mask |= (uint8_t)(1u << slot);
                    std::snprintf(g_sess.names[slot], kMaxNameLen, "%s",
                                  hello.name[0] ? hello.name : "3DS");
                    int cnt = 0;
                    for (int i = 0; i < kMaxPlayers; ++i) if (g_sess.occupied_mask & (1u << i)) cnt++;
                    g_sess.player_count = cnt;
                    wel.assigned_port = (uint8_t)slot;
                    wel.player_count = (uint8_t)g_sess.player_count;
                    wel.input_delay = (uint8_t)g_sess.input_delay;
                    wel.accepted = 1;
                    redraw = true;
                } else {
                    wel.accepted = 0;
                }
                uint8_t out[kMaxPacketBytes];
                const size_t n = encode_welcome(wel, out, sizeof(out));
                send_packet(UDS_BROADCAST_NETWORKNODEID, out, n);
                broadcast_lobby_state();
            }
        }

        const u64 now = osGetTime();
        if (now - last_bcast >= 500) {
            broadcast_lobby_state();
            last_bcast = now;
        }

        if (redraw) {
            char msg[160];
            std::snprintf(msg, sizeof(msg), "P1: %s  P2: %s\nP3: %s  P4: %s",
                          g_sess.names[0][0] ? g_sess.names[0] : "---",
                          g_sess.names[1][0] ? g_sess.names[1] : "---",
                          g_sess.names[2][0] ? g_sess.names[2] : "---",
                          g_sess.names[3][0] ? g_sess.names[3] : "---");
            char detail[64];
            std::snprintf(detail, sizeof(detail), "PLAYERS: %d / 4  (DELAY %d)", g_sess.player_count, g_sess.input_delay);
            draw_lobby(top, bottom, "HOSTING LOCAL WIRELESS", msg, detail,
                       g_sess.player_count >= 2 ? "A: START MATCH   B: CANCEL" : "WAITING FOR PLAYERS...  B: CANCEL");
            redraw = false;
        }
        gspWaitForVBlank();
    }
    return false;
}

bool run_join_lobby(PrintConsole* top, PrintConsole* bottom) {
    constexpr size_t kScanBufSize = 0x4000;
    void* scan_buf = std::malloc(kScanBufSize);
    if (!scan_buf) return false;

    udsNetworkStruct found_net{};
    bool found = false;
    u64 last_scan = 0;
    drain_buttons();

    draw_lobby(top, bottom, "JOIN LOCAL WIRELESS", "Scanning for a nearby Conker 3DS host...",
               "OPEN HOST ON ANOTHER 3DS", "B: CANCEL");

    while (aptMainLoop() && !found) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) {
            std::free(scan_buf);
            return false;
        }
        const u64 now = osGetTime();
        if (now - last_scan >= 600) {
            last_scan = now;
            udsNetworkScanInfo* networks = nullptr;
            size_t total = 0;
            const Result rc = udsScanBeacons(scan_buf, kScanBufSize, &networks, &total,
                                             kWlanCommId, kWlanId8, nullptr, false);
            if (R_SUCCEEDED(rc) && total > 0 && networks != nullptr) {
                found_net = networks[0].network;
                found = true;
            }
            if (networks) std::free(networks);
        }
        gspWaitForVBlank();
    }
    std::free(scan_buf);
    if (!found) return false;

    draw_lobby(top, bottom, "JOIN LOCAL WIRELESS", "Host found! Connecting...", "LOCAL WIRELESS", "B: CANCEL");
    const Result crc = udsConnectNetwork(&found_net, kPassphrase, sizeof(kPassphrase),
                                         &g_sess.bind, UDS_BROADCAST_NETWORKNODEID,
                                         UDSCONTYPE_Client, kDataChannel, UDS_DEFAULT_RECVBUFSIZE);
    if (R_FAILED(crc)) {
        char err[96];
        std::snprintf(err, sizeof(err), "Could not connect (0x%08lx).", (unsigned long)crc);
        draw_lobby(top, bottom, "CONNECTION FAILED", err, "TRY AGAIN", "B: BACK");
        drain_buttons();
        while (aptMainLoop()) {
            hidScanInput();
            if (hidKeysDown() & (KEY_A | KEY_B)) break;
            gspWaitForVBlank();
        }
        return false;
    }

    g_sess.hosting = false;
    g_sess.connected = true;
    g_sess.bound = true;

    const uint32_t my_client_id = ((uint32_t)osGetTime() ^ (uint32_t)svcGetSystemTick()) | 1u;

    bool welcomed = false;
    u64 last_hello = 0;
    int rx_w = 0, rx_l = 0, rx_s = 0;
    bool redraw = true;
    drain_buttons();

    while (aptMainLoop()) {
        hidScanInput();
        if (hidKeysDown() & KEY_B) return false;

        const u64 now = osGetTime();
        if (!welcomed && now - last_hello >= 350) {
            HelloPacket hello{};
            hello.rom_hash = g_sess.rom_hash;
            hello.client_id = my_client_id;
            std::snprintf(hello.name, kMaxNameLen, "%s", g_sess.local_name);
            uint8_t out[kMaxPacketBytes];
            const size_t n = encode_hello(hello, out, sizeof(out));
            send_packet(UDS_HOST_NETWORKNODEID, out, n);
            last_hello = now;
        }

        uint8_t raw[kMaxPacketBytes];
        size_t actual = 0;
        u16 src_node = 0;
        while (R_SUCCEEDED(udsPullPacket(&g_sess.bind, raw, sizeof(raw), &actual, &src_node)) && actual > 0) {
            PacketKind kind{};
            if (!peek_packet_kind(raw, actual, kind)) continue;
            if (kind == PacketKind::Welcome) {
                rx_w++;
                WelcomePacket wel{};
                if (decode_welcome(raw, actual, wel) && wel.client_id == my_client_id) {
                    if (!wel.accepted) {
                        draw_lobby(top, bottom, "JOIN REFUSED",
                                   "Host rejected join (different ROM version or room full).",
                                   "USE THE SAME ROM", "B: BACK");
                        drain_buttons();
                        while (aptMainLoop()) {
                            hidScanInput();
                            if (hidKeysDown() & (KEY_A | KEY_B)) break;
                            gspWaitForVBlank();
                        }
                        return false;
                    }
                    welcomed = true;
                    g_sess.local_port = wel.assigned_port;
                    g_sess.player_count = wel.player_count;
                    g_sess.input_delay = wel.input_delay;
                    redraw = true;
                }
            } else if (kind == PacketKind::LobbyState) {
                rx_l++;
                LobbyStatePacket ls{};
                if (decode_lobby_state(raw, actual, ls)) {
                    g_sess.player_count = ls.player_count;
                    g_sess.input_delay = ls.input_delay;
                    g_sess.occupied_mask = ls.occupied_mask;
                    std::memcpy(g_sess.names, ls.names, sizeof(g_sess.names));
                    if (!welcomed) {
                        for (int i = 1; i < kMaxPlayers; ++i) {
                            if (ls.client_ids[i] == my_client_id) {
                                welcomed = true;
                                g_sess.local_port = i;
                                break;
                            }
                        }
                    }
                    if (ls.started && g_sess.player_count >= 2) {
                        if (!welcomed) g_sess.local_port = 1;
                        g_sess.match_active = true;
                        return true;
                    }
                    redraw = true;
                }
            } else if (kind == PacketKind::StartMatch) {
                rx_s++;
                StartMatchPacket sp{};
                if (decode_start_match(raw, actual, sp)) {
                    if (!welcomed) g_sess.local_port = 1;
                    g_sess.player_count = sp.player_count;
                    g_sess.input_delay = sp.input_delay;
                    g_sess.occupied_mask = sp.occupied_mask;
                    g_sess.match_active = true;
                    return true;
                }
            } else if (kind == PacketKind::Inputs) {
                InputsPacket ip{};
                if (decode_inputs(raw, actual, ip) && ip.port == 0 && g_sess.player_count >= 2) {
                    if (!welcomed) g_sess.local_port = 1;
                    g_sess.match_active = true;
                    return true;
                }
            }
        }

        if (redraw) {
            char msg[160];
            if (!welcomed) {
                std::snprintf(msg, sizeof(msg), "Connected to host. Handshaking...");
            } else {
                std::snprintf(msg, sizeof(msg), "You are Player %d\nP1: %s  P2: %s\nP3: %s  P4: %s",
                              g_sess.local_port + 1,
                              g_sess.names[0][0] ? g_sess.names[0] : "---",
                              g_sess.names[1][0] ? g_sess.names[1] : "---",
                              g_sess.names[2][0] ? g_sess.names[2] : "---",
                              g_sess.names[3][0] ? g_sess.names[3] : "---");
            }
            char detail[64];
            std::snprintf(detail, sizeof(detail), "WAIT HOST (%d/4) W:%d L:%d S:%d",
                          g_sess.player_count, rx_w, rx_l, rx_s);
            draw_lobby(top, bottom, "JOINED LOCAL WIRELESS", msg, detail, "B: LEAVE");
            redraw = false;
        }
        gspWaitForVBlank();
    }
    return false;
}

} // namespace

bool conker_netplay::run_local_wireless_lobby(PrintConsole* top, PrintConsole* bottom, uint64_t rom_hash) {
    cleanup_network();
    g_sess.rom_hash = rom_hash;
    load_local_username(g_sess.local_name);

    const char* labels[3] = { "Host Room (Player 1)", "Join Room (Player 2-4)", "Back" };
    const char* notes[3] = { "Create a Local Wireless room", "Scan for a nearby 3DS host", "Return to ROM selection" };
    const int avail[3] = { 1, 1, 1 };
    int selected = 0;
    bool redraw = true;
    drain_buttons();

    while (aptMainLoop()) {
        hidScanInput();
        const u32 down = hidKeysDown();
        if (down & (KEY_UP | KEY_DOWN)) {
            selected = (selected + ((down & KEY_UP) ? 2 : 1)) % 3;
            redraw = true;
        }
        if (down & KEY_B) return false;
        if (down & (KEY_A | KEY_START)) {
            if (selected == 2) return false;
            const Result urc = udsInit(kUdsSharedMemSize, nullptr);
            if (R_FAILED(urc)) {
                char err[96];
                std::snprintf(err, sizeof(err), "udsInit failed (0x%08lx). Turn wireless on.", (unsigned long)urc);
                draw_lobby(top, bottom, "LOCAL WIRELESS ERROR", err, "CHECK WI-FI SWITCH", "B: BACK");
                drain_buttons();
                while (aptMainLoop()) {
                    hidScanInput();
                    if (hidKeysDown() & (KEY_A | KEY_B)) break;
                    gspWaitForVBlank();
                }
                redraw = true;
                continue;
            }
            g_sess.uds_inited = true;
            const bool ok = (selected == 0) ? run_host_lobby(top, bottom) : run_join_lobby(top, bottom);
            if (ok) return true;
            cleanup_network();
            redraw = true;
        }
        if (redraw) {
            draw_lobby(top, bottom, "LOCAL WIRELESS",
                       "Play 2-4 player multiplayer over 3DS Local Wireless (same ROM required).",
                       "BOTH 3DS CONSOLES BOOT TOGETHER",
                       "A: SELECT   B: BACK",
                       labels, notes, avail, selected, 1);
            redraw = false;
        }
        gspWaitForVBlank();
    }
    cleanup_network();
    return false;
}

bool conker_netplay::is_active() { return g_sess.match_active; }
int conker_netplay::player_count() { return g_sess.match_active ? g_sess.player_count : 1; }
int conker_netplay::local_port() { return g_sess.match_active ? g_sess.local_port : 0; }

void conker_netplay::on_game_init() {
    if (!g_sess.match_active) return;
    g_sess.engine.start(g_sess.local_port, g_sess.player_count, g_sess.input_delay);
    const u64 now = osGetTime();
    for (int i = 0; i < kMaxPlayers; ++i) g_sess.last_peer_ms[i] = now;
    ultramodern::input::set_read_data_callback(netplay_read_data_callback);
    ultramodern::timeline::enable(false);
    std::fprintf(stderr, "netplay: started local wireless match (port %d of %d, delay %d)\n",
                 g_sess.local_port + 1, g_sess.player_count, g_sess.input_delay);
}

void conker_netplay::shutdown() {
    if (g_sess.connected && g_sess.match_active) {
        LeavePacket lp{};
        lp.port = (uint8_t)g_sess.local_port;
        uint8_t raw[kMaxPacketBytes];
        const size_t n = encode_leave(lp, raw, sizeof(raw));
        send_packet(g_sess.hosting ? UDS_BROADCAST_NETWORKNODEID : UDS_HOST_NETWORKNODEID, raw, n);
    }
    ultramodern::input::set_read_data_callback(nullptr);
    ultramodern::timeline::disable();
    cleanup_network();
}
