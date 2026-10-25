#include "network/udp_transport.hpp"

#include <algorithm>
#include <array>

#include "network/protocol.hpp"

namespace mordor::net {

namespace {
constexpr int kMaxDatagramsPerPoll = 512;
}

UdpTransport::UdpTransport(const ImpairmentConfig& impairment) : sender_(socket_, impairment) {}

bool UdpTransport::open(const Endpoint& bind, bool, std::string* error) { return socket_.open(bind, error); }

void UdpTransport::close() {
    stats_.discarded_on_close += sender_.queued();  // accepted but never transmitted: counted, not silent
    sender_.clear();
    socket_.close();
    by_endpoint_.clear();
    by_id_.clear();
    activity_.clear();
    by_time_.clear();
    by_count_.clear();
    pending_.clear();
}

TransportCapabilities UdpTransport::capabilities() const {
    return {.reliable_ordered = false, .reports_disconnects = false, .max_payload = kMaxPacketSize,
            .exact_loss_accounting = true, .per_instance_impairment = true};
}

PeerId UdpTransport::peer_for(const Endpoint& e) {
    if (auto it = by_endpoint_.find(e); it != by_endpoint_.end()) return it->second;
    if (by_endpoint_.size() >= limits_.max_peers && !by_count_.empty()) forget(std::get<2>(*by_count_.begin()), "evicted_table_full");
    const auto g = generations_.find(e);
    const PeerId p{next_id_++, g == generations_.end() ? 0u : g->second};
    if (g != generations_.end()) generations_.erase(g);  // live again: tracked by the peer itself
    by_endpoint_[e] = p;
    by_id_[p.id] = e;
    touch(p.id);
    pending_.push_back({TransportEvent::Kind::Connected, p, e, Delivery::Unreliable, {}, "first_datagram"});
    return p;
}

void UdpTransport::touch(std::uint32_t id) {
    auto [it, inserted] = activity_.try_emplace(id);
    auto& a = it->second;
    if (!inserted) {
        by_time_.erase({a.last_us, id});
        by_count_.erase({a.datagrams, a.last_us, id});
    }
    a.last_us = now_us_;
    ++a.datagrams;
    by_time_.insert({a.last_us, id});
    by_count_.insert({a.datagrams, a.last_us, id});
}

void UdpTransport::forget(std::uint32_t id, std::string_view reason) {
    const auto it = by_id_.find(id);
    if (it == by_id_.end()) return;
    const Endpoint e = it->second;
    const PeerId p = by_endpoint_.at(e);
    pending_.push_back({TransportEvent::Kind::Disconnected, p, e, Delivery::Unreliable, {}, reason});
    if (generations_.size() >= limits_.max_peers) generations_.erase(generations_.begin());  // bounded history
    generations_[e] = p.generation + 1;
    if (auto a = activity_.find(id); a != activity_.end()) {
        by_time_.erase({a->second.last_us, id});
        by_count_.erase({a->second.datagrams, a->second.last_us, id});
        activity_.erase(a);
    }
    by_endpoint_.erase(e);
    by_id_.erase(it);
}

std::optional<PeerId> UdpTransport::connect(const Endpoint& remote) {
    if (!is_open()) return std::nullopt;
    return peer_for(remote);
}

SendResult UdpTransport::send(PeerId peer, std::span<const std::uint8_t> bytes, Delivery d, std::uint64_t now_us) {
    auto fail = [&](SendResult r, std::uint64_t& counter) {
        ++counter;
        return r;
    };
    if (!is_open()) return SendResult::Closed;
    now_us_ = std::max(now_us_, now_us);
    if (d != Delivery::Unreliable) return fail(SendResult::Unsupported, stats_.unsupported);
    if (bytes.size() > kMaxPacketSize) return fail(SendResult::TooLarge, stats_.too_large);
    const auto it = by_id_.find(peer.id);
    if (it == by_id_.end() || by_endpoint_.at(it->second) != peer) return fail(SendResult::NotConnected, stats_.not_connected);
    touch(peer.id);  // traffic we send keeps the peer too
    switch (sender_.send(it->second, std::vector<std::uint8_t>(bytes.begin(), bytes.end()), now_us)) {
        case ImpairOutcome::Failed: return fail(SendResult::Failed, stats_.send_failed);
        case ImpairOutcome::Sent:
        case ImpairOutcome::Queued:
        case ImpairOutcome::SimulatedDrop: break;  // network loss is not a local error
    }
    ++stats_.sent;
    return SendResult::Sent;
}

void UdpTransport::poll(std::uint64_t now_us, std::vector<TransportEvent>& out) {
    now_us_ = std::max(now_us_, now_us);
    // Peers made by connect() before any clock was seen carry time 0: stamp them now, never expire them for it.
    while (now_us_ > 0 && !by_time_.empty() && by_time_.begin()->first == 0) touch(by_time_.begin()->second);
    while (!by_time_.empty() && now_us_ - by_time_.begin()->first > limits_.idle_expiry_us) forget(by_time_.begin()->second, "idle_expired");
    sender_.flush(now_us);
    out.insert(out.end(), std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.end()));
    pending_.clear();
    if (!is_open()) return;
    std::array<std::uint8_t, 2048> buf{};
    for (int i = 0; i < kMaxDatagramsPerPoll; ++i) {
        Endpoint from;
        const auto r = socket_.recv_from(from, buf);
        if (r.status == RecvStatus::Empty || r.status == RecvStatus::Error) break;
        if (r.status == RecvStatus::Oversized) {
            // Do not create a peer for an oversized datagram; report it so callers can count it.
            ++stats_.dropped_inbound_oversized;
            const auto known = by_endpoint_.find(from);
            out.push_back({TransportEvent::Kind::DroppedInbound, known == by_endpoint_.end() ? PeerId{} : known->second, from,
                           Delivery::Unreliable, {}, "oversized_datagram"});
            continue;
        }
        const PeerId p = peer_for(from);  // may queue Connected (and an eviction); deliver them before the message
        touch(p.id);  // counts this datagram (peer_for counted the creation)
        out.insert(out.end(), std::make_move_iterator(pending_.begin()), std::make_move_iterator(pending_.end()));
        pending_.clear();
        ++stats_.received;
        out.push_back({TransportEvent::Kind::Message, p, from, Delivery::Unreliable,
                       std::vector<std::uint8_t>(buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(r.size)), {}});
    }
}

void UdpTransport::close_peer(PeerId peer) {
    const auto it = by_id_.find(peer.id);
    if (it == by_id_.end() || by_endpoint_.at(it->second) != peer) return;
    forget(peer.id, "closed_locally");
}

const TransportStats& UdpTransport::stats() const {
    stats_.simulated_drops = sender_.stats().dropped;
    stats_.accepted_then_failed = sender_.stats().delayed_send_failures;
    return stats_;
}

}  // namespace mordor::net
