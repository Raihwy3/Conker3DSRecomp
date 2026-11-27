#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace conker_netplay {

constexpr uint16_t kProtocolVersion = 1;
constexpr int kMaxPlayers = 4;
constexpr int kInputWindow = 8;
constexpr int kDefaultInputDelay = 3;
constexpr size_t kMaxPacketBytes = 160;
constexpr size_t kMaxNameLen = 12;

enum class PacketKind : uint8_t {
    Hello = 1,
    Welcome = 2,
    LobbyState = 3,
    StartMatch = 4,
    Inputs = 5,
    Leave = 6,
};

struct Pad {
    uint16_t button = 0;
    int8_t stick_x = 0;
    int8_t stick_y = 0;
    uint8_t err_no = 0;

    bool operator==(const Pad& o) const {
        return button == o.button && stick_x == o.stick_x &&
               stick_y == o.stick_y && err_no == o.err_no;
    }
};

struct HelloPacket {
    uint64_t rom_hash = 0;
    uint32_t client_id = 0;
    char name[kMaxNameLen]{};
};

struct WelcomePacket {
    uint32_t client_id = 0;
    uint8_t assigned_port = 0;
    uint8_t player_count = 1;
    uint8_t input_delay = kDefaultInputDelay;
    uint8_t accepted = 1;
};

struct LobbyStatePacket {
    uint8_t player_count = 1;
    uint8_t input_delay = kDefaultInputDelay;
    uint8_t occupied_mask = 1;
    uint8_t started = 0;
    uint32_t client_ids[kMaxPlayers]{};
    char names[kMaxPlayers][kMaxNameLen]{};
};

struct StartMatchPacket {
    uint8_t player_count = 2;
    uint8_t input_delay = kDefaultInputDelay;
    uint8_t occupied_mask = 3;
    uint8_t own_view = 0;
};

struct InputsPacket {
    uint8_t port = 0;
    uint8_t count = 0; // 1..kInputWindow
    uint32_t first_read = 0;
    Pad pads[kInputWindow]{};
};

struct LeavePacket {
    uint8_t port = 0;
    uint8_t reason = 0;
};

namespace detail {
inline void put_u8(uint8_t*& p, uint8_t v) { *p++ = v; }
inline void put_u16(uint8_t*& p, uint16_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p += 2;
}
inline void put_u32(uint8_t*& p, uint32_t v) {
    p[0] = (uint8_t)(v & 0xFFu);
    p[1] = (uint8_t)((v >> 8) & 0xFFu);
    p[2] = (uint8_t)((v >> 16) & 0xFFu);
    p[3] = (uint8_t)((v >> 24) & 0xFFu);
    p += 4;
}
inline void put_u64(uint8_t*& p, uint64_t v) {
    for (int i = 0; i < 8; ++i) p[i] = (uint8_t)((v >> (i * 8)) & 0xFFu);
    p += 8;
}
inline uint8_t get_u8(const uint8_t*& p) { return *p++; }
inline uint16_t get_u16(const uint8_t*& p) {
    const uint16_t v = (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
    p += 2;
    return v;
}
inline uint32_t get_u32(const uint8_t*& p) {
    const uint32_t v = (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
    p += 4;
    return v;
}
inline uint64_t get_u64(const uint8_t*& p) {
    uint64_t v = 0;
    for (int i = 0; i < 8; ++i) v |= (uint64_t)p[i] << (i * 8);
    p += 8;
    return v;
}
inline void write_header(uint8_t*& p, PacketKind kind) {
    put_u8(p, 'C');
    put_u8(p, '3');
    put_u16(p, kProtocolVersion);
    put_u8(p, static_cast<uint8_t>(kind));
}
inline bool read_header(const uint8_t*& p, size_t len, PacketKind& kind) {
    if (len < 5) return false;
    if (p[0] != 'C' || p[1] != '3') return false;
    p += 2;
    if (get_u16(p) != kProtocolVersion) return false;
    kind = static_cast<PacketKind>(get_u8(p));
    return true;
}
} // namespace detail

inline size_t encode_hello(const HelloPacket& pkt, uint8_t* out, size_t cap) {
    if (cap < 5 + 12 + kMaxNameLen) return 0;
    uint8_t* p = out;
    detail::write_header(p, PacketKind::Hello);
    detail::put_u64(p, pkt.rom_hash);
    detail::put_u32(p, pkt.client_id);
    std::memcpy(p, pkt.name, kMaxNameLen);
    p += kMaxNameLen;
    return (size_t)(p - out);
}

inline bool decode_hello(const uint8_t* data, size_t len, HelloPacket& out) {
    const uint8_t* p = data;
    PacketKind kind{};
    if (!detail::read_header(p, len, kind) || kind != PacketKind::Hello) return false;
    if (len != 5 + 12 + kMaxNameLen) return false;
    out.rom_hash = detail::get_u64(p);
    out.client_id = detail::get_u32(p);
    std::memcpy(out.name, p, kMaxNameLen);
    out.name[kMaxNameLen - 1] = '\0';
    return true;
}

inline size_t encode_welcome(const WelcomePacket& pkt, uint8_t* out, size_t cap) {
    if (cap < 5 + 8) return 0;
    uint8_t* p = out;
    detail::write_header(p, PacketKind::Welcome);
    detail::put_u32(p, pkt.client_id);
    detail::put_u8(p, pkt.assigned_port);
    detail::put_u8(p, pkt.player_count);
    detail::put_u8(p, pkt.input_delay);
    detail::put_u8(p, pkt.accepted);
    return (size_t)(p - out);
}

inline bool decode_welcome(const uint8_t* data, size_t len, WelcomePacket& out) {
    const uint8_t* p = data;
    PacketKind kind{};
    if (!detail::read_header(p, len, kind) || kind != PacketKind::Welcome) return false;
    if (len != 5 + 8) return false;
    out.client_id = detail::get_u32(p);
    out.assigned_port = detail::get_u8(p);
    out.player_count = detail::get_u8(p);
    out.input_delay = detail::get_u8(p);
    out.accepted = detail::get_u8(p);
    return out.assigned_port < kMaxPlayers && out.player_count >= 1 && out.player_count <= kMaxPlayers;
}

inline size_t encode_lobby_state(const LobbyStatePacket& pkt, uint8_t* out, size_t cap) {
    const size_t need = 5 + 4 + kMaxPlayers * 4 + kMaxPlayers * kMaxNameLen;
    if (cap < need) return 0;
    uint8_t* p = out;
    detail::write_header(p, PacketKind::LobbyState);
    detail::put_u8(p, pkt.player_count);
    detail::put_u8(p, pkt.input_delay);
    detail::put_u8(p, pkt.occupied_mask);
    detail::put_u8(p, pkt.started);
    for (int i = 0; i < kMaxPlayers; ++i) {
        detail::put_u32(p, pkt.client_ids[i]);
    }
    for (int i = 0; i < kMaxPlayers; ++i) {
        std::memcpy(p, pkt.names[i], kMaxNameLen);
        p += kMaxNameLen;
    }
    return (size_t)(p - out);
}

inline bool decode_lobby_state(const uint8_t* data, size_t len, LobbyStatePacket& out) {
    const size_t need = 5 + 4 + kMaxPlayers * 4 + kMaxPlayers * kMaxNameLen;
    const uint8_t* p = data;
    PacketKind kind{};
    if (!detail::read_header(p, len, kind) || kind != PacketKind::LobbyState) return false;
    if (len != need) return false;
    out.player_count = detail::get_u8(p);
    out.input_delay = detail::get_u8(p);
    out.occupied_mask = detail::get_u8(p);
    out.started = detail::get_u8(p);
    for (int i = 0; i < kMaxPlayers; ++i) {
        out.client_ids[i] = detail::get_u32(p);
    }
    for (int i = 0; i < kMaxPlayers; ++i) {
        std::memcpy(out.names[i], p, kMaxNameLen);
        out.names[i][kMaxNameLen - 1] = '\0';
        p += kMaxNameLen;
    }
    return out.player_count >= 1 && out.player_count <= kMaxPlayers;
}

inline size_t encode_start_match(const StartMatchPacket& pkt, uint8_t* out, size_t cap) {
    if (cap < 5 + 4) return 0;
    uint8_t* p = out;
    detail::write_header(p, PacketKind::StartMatch);
    detail::put_u8(p, pkt.player_count);
    detail::put_u8(p, pkt.input_delay);
    detail::put_u8(p, pkt.occupied_mask);
    detail::put_u8(p, pkt.own_view);
    return (size_t)(p - out);
}

inline bool decode_start_match(const uint8_t* data, size_t len, StartMatchPacket& out) {
    const uint8_t* p = data;
    PacketKind kind{};
    if (!detail::read_header(p, len, kind) || kind != PacketKind::StartMatch) return false;
    if (len != 5 + 4) return false;
    out.player_count = detail::get_u8(p);
    out.input_delay = detail::get_u8(p);
    out.occupied_mask = detail::get_u8(p);
    out.own_view = detail::get_u8(p);
    return out.player_count >= 2 && out.player_count <= kMaxPlayers && out.input_delay <= 8;
}

inline size_t encode_inputs(const InputsPacket& pkt, uint8_t* out, size_t cap) {
    if (pkt.port >= kMaxPlayers || pkt.count == 0 || pkt.count > kInputWindow) return 0;
    const size_t need = 5 + 6 + (size_t)pkt.count * 5;
    if (cap < need) return 0;
    uint8_t* p = out;
    detail::write_header(p, PacketKind::Inputs);
    detail::put_u8(p, pkt.port);
    detail::put_u8(p, pkt.count);
    detail::put_u32(p, pkt.first_read);
    for (uint8_t i = 0; i < pkt.count; ++i) {
        detail::put_u16(p, pkt.pads[i].button);
        detail::put_u8(p, (uint8_t)pkt.pads[i].stick_x);
        detail::put_u8(p, (uint8_t)pkt.pads[i].stick_y);
        detail::put_u8(p, pkt.pads[i].err_no);
    }
    return (size_t)(p - out);
}

inline bool decode_inputs(const uint8_t* data, size_t len, InputsPacket& out) {
    const uint8_t* p = data;
    PacketKind kind{};
    if (!detail::read_header(p, len, kind) || kind != PacketKind::Inputs) return false;
    if (len < 5 + 6) return false;
    out.port = detail::get_u8(p);
    out.count = detail::get_u8(p);
    out.first_read = detail::get_u32(p);
    if (out.port >= kMaxPlayers || out.count == 0 || out.count > kInputWindow) return false;
    if (len != 5 + 6 + (size_t)out.count * 5) return false;
    for (uint8_t i = 0; i < out.count; ++i) {
        out.pads[i].button = detail::get_u16(p);
        out.pads[i].stick_x = (int8_t)detail::get_u8(p);
        out.pads[i].stick_y = (int8_t)detail::get_u8(p);
        out.pads[i].err_no = detail::get_u8(p);
    }
    return true;
}

inline size_t encode_leave(const LeavePacket& pkt, uint8_t* out, size_t cap) {
    if (cap < 5 + 2) return 0;
    uint8_t* p = out;
    detail::write_header(p, PacketKind::Leave);
    detail::put_u8(p, pkt.port);
    detail::put_u8(p, pkt.reason);
    return (size_t)(p - out);
}

inline bool decode_leave(const uint8_t* data, size_t len, LeavePacket& out) {
    const uint8_t* p = data;
    PacketKind kind{};
    if (!detail::read_header(p, len, kind) || kind != PacketKind::Leave) return false;
    if (len != 5 + 2) return false;
    out.port = detail::get_u8(p);
    out.reason = detail::get_u8(p);
    return out.port < kMaxPlayers;
}

inline bool peek_packet_kind(const uint8_t* data, size_t len, PacketKind& kind) {
    const uint8_t* p = data;
    return detail::read_header(p, len, kind);
}

} // namespace conker_netplay
