#include "network/udp_transport.hpp"

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
    pending_.clear();
}

TransportCapabilities UdpTransport::capabilities() const {
    return {.reliable_ordered = false, .reports_disconnects = false, .max_payload = kMaxPacketSize};
}

PeerId UdpTransport::peer_for(const Endpoint& e) {
    if (auto it = by_endpoint_.find(e); it != by_endpoint_.end()) return it->second;
    const PeerId p{next_id_++, generations_[e]++};
    by_endpoint_[e] = p;
    by_id_[p.id] = e;
    pending_.push_back({TransportEvent::Kind::Connected, p, e, Delivery::Unreliable, {}, "first_datagram"});
    return p;
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
    if (d != Delivery::Unreliable) return fail(SendResult::Unsupported, stats_.unsupported);
    if (bytes.size() > kMaxPacketSize) return fail(SendResult::TooLarge, stats_.too_large);
    const auto it = by_id_.find(peer.id);
    if (it == by_id_.end() || by_endpoint_.at(it->second) != peer) return fail(SendResult::NotConnected, stats_.not_connected);
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
        const PeerId p = peer_for(from);  // may queue Connected; deliver it before the message
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
    pending_.push_back({TransportEvent::Kind::Disconnected, peer, it->second, Delivery::Unreliable, {}, "closed_locally"});
    by_endpoint_.erase(it->second);
    by_id_.erase(it);
}

const TransportStats& UdpTransport::stats() const {
    stats_.simulated_drops = sender_.stats().dropped;
    stats_.accepted_then_failed = sender_.stats().delayed_send_failures;
    return stats_;
}

}  // namespace mordor::net
