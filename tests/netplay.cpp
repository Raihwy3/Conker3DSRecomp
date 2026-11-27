#include <cassert>
#include <cstdio>
#include <cstring>
#include <random>
#include <string_view>
#include <vector>

#include "../game/platform/3ds/netplay/netplay_packets.h"
#include "../game/platform/3ds/netplay/netplay_lockstep.h"

using namespace conker_netplay;

static void test_packet_roundtrip() {
    uint8_t buf[kMaxPacketBytes]{};

    HelloPacket h{};
    h.rom_hash = 0x1122334455667788ull;
    h.client_id = 0xA1B2C3D5u;
    std::snprintf(h.name, kMaxNameLen, "Conker1");
    size_t n = encode_hello(h, buf, sizeof(buf));
    assert(n > 0);
    HelloPacket h2{};
    assert(decode_hello(buf, n, h2));
    assert(h2.rom_hash == h.rom_hash && h2.client_id == h.client_id && std::string_view(h2.name) == "Conker1");

    WelcomePacket w{ 0xA1B2C3D5u, 2, 3, 4, 1 };
    n = encode_welcome(w, buf, sizeof(buf));
    WelcomePacket w2{};
    assert(decode_welcome(buf, n, w2));
    assert(w2.client_id == 0xA1B2C3D5u && w2.assigned_port == 2 && w2.player_count == 3 && w2.input_delay == 4 && w2.accepted == 1);

    LobbyStatePacket ls{};
    ls.player_count = 2;
    ls.input_delay = 3;
    ls.occupied_mask = 3;
    ls.started = 1;
    ls.client_ids[0] = 1u;
    ls.client_ids[1] = 0xA1B2C3D5u;
    std::snprintf(ls.names[0], kMaxNameLen, "P1");
    std::snprintf(ls.names[1], kMaxNameLen, "P2");
    n = encode_lobby_state(ls, buf, sizeof(buf));
    LobbyStatePacket ls2{};
    assert(decode_lobby_state(buf, n, ls2));
    assert(ls2.player_count == 2 && ls2.started == 1 && ls2.client_ids[1] == 0xA1B2C3D5u && std::string_view(ls2.names[1]) == "P2");

    StartMatchPacket s{ 4, 3, 0x0F, 1 };
    n = encode_start_match(s, buf, sizeof(buf));
    StartMatchPacket s2{};
    assert(decode_start_match(buf, n, s2));
    assert(s2.player_count == 4 && s2.input_delay == 3 && s2.occupied_mask == 0x0F && s2.own_view == 1);

    InputsPacket ip{};
    ip.port = 1;
    ip.count = 3;
    ip.first_read = 42;
    ip.pads[0] = Pad{ 0x8000, 12, -34, 0 };
    ip.pads[1] = Pad{ 0x4000, -50, 60, 0 };
    ip.pads[2] = Pad{ 0x0010, 0, 0, 0 };
    n = encode_inputs(ip, buf, sizeof(buf));
    InputsPacket ip2{};
    assert(decode_inputs(buf, n, ip2));
    assert(ip2.port == 1 && ip2.count == 3 && ip2.first_read == 42);
    for (int i = 0; i < 3; ++i) assert(ip2.pads[i] == ip.pads[i]);

    // Corrupted magic must be rejected
    buf[0] = 'X';
    assert(!decode_inputs(buf, n, ip2));
}

static void test_lockstep_4p_with_packet_loss() {
    constexpr int kPlayers = 4;
    constexpr int kDelay = 3;
    constexpr int kFrames = 200;

    LockstepEngine peers[kPlayers];
    for (int p = 0; p < kPlayers; ++p) {
        peers[p].start(p, kPlayers, kDelay);
    }

    std::mt19937 rng(0xC0FFEEu);
    std::vector<std::array<Pad, kMaxPlayers>> history[kPlayers];

    for (int f = 0; f < kFrames; ++f) {
        InputsPacket pkts[kPlayers];
        for (int p = 0; p < kPlayers; ++p) {
            Pad pad{};
            pad.button = (uint16_t)((f * 13 + p * 7) & 0xFFFFu);
            pad.stick_x = (int8_t)((f + p * 17) % 80 - 40);
            pad.stick_y = (int8_t)((f * 3 - p * 11) % 80 - 40);
            pkts[p] = peers[p].push_local_input(pad);
        }
        // Deliver with 25% random packet loss — sliding window of 8 recovers seamlessly
        for (int src = 0; src < kPlayers; ++src) {
            for (int dst = 0; dst < kPlayers; ++dst) {
                if (src == dst) continue;
                if ((rng() % 4) != 0) {
                    peers[dst].on_remote_inputs(pkts[src]);
                }
            }
        }
        // If any peer stalled due to consecutive drops, resend its sliding window once
        for (int dst = 0; dst < kPlayers; ++dst) {
            if (!peers[dst].ready_to_advance()) {
                for (int src = 0; src < kPlayers; ++src) {
                    if (src != dst) peers[dst].on_remote_inputs(peers[src].build_local_packet());
                }
            }
            assert(peers[dst].ready_to_advance());
            std::array<Pad, kMaxPlayers> out{};
            assert(peers[dst].pop_frame(out.data()));
            history[dst].push_back(out);
        }
    }

    // All 4 peers must have observed bit-identical Pad[4] on every single read
    for (int p = 1; p < kPlayers; ++p) {
        assert(history[p].size() == history[0].size());
        for (size_t i = 0; i < history[0].size(); ++i) {
            for (int port = 0; port < kPlayers; ++port) {
                assert(history[p][i][port] == history[0][i][port]);
            }
        }
    }
}

static void test_peer_disconnect() {
    LockstepEngine p0, p1;
    p0.start(0, 2, 2);
    p1.start(1, 2, 2);
    for (int f = 0; f < 5; ++f) {
        auto a = p0.push_local_input(Pad{ (uint16_t)(f + 1), 0, 0, 0 });
        auto b = p1.push_local_input(Pad{ (uint16_t)(f + 10), 0, 0, 0 });
        p0.on_remote_inputs(b);
        p1.on_remote_inputs(a);
        Pad out[kMaxPlayers]{};
        assert(p0.pop_frame(out));
        assert(p1.pop_frame(out));
    }
    // Peer 1 leaves; peer 0 must still advance cleanly with neutral pad for port 1
    p0.mark_peer_left(1);
    p0.push_local_input(Pad{ 0x1234, 10, 20, 0 });
    assert(p0.ready_to_advance());
    Pad out[kMaxPlayers]{};
    assert(p0.pop_frame(out));
    assert(out[1] == Pad{});
}

int main() {
    test_packet_roundtrip();
    test_lockstep_4p_with_packet_loss();
    test_peer_disconnect();
    std::printf("PASS netplay: packet roundtrip, 4-player lockstep with 25%% packet loss, disconnect recovery\n");
    return 0;
}
