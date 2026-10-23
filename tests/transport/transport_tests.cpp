// Backend-independent Transport contract tests (review #4 FB-4-01/02/04). Every test runs against every backend
// available in this build (net::transport_available), so the GNS adapter must pass the same contract.
#include <chrono>

#include "network/transport.hpp"
#include "test_support.hpp"

using namespace mordor;
using namespace mordor::net;

namespace {

std::vector<TransportKind> backends() {
    std::vector<TransportKind> out;
    for (auto k : {TransportKind::Udp, TransportKind::Gns})
        if (transport_available(k)) out.push_back(k);
    return out;
}

std::unique_ptr<Transport> make_open(TransportKind k, bool listen, ImpairmentConfig imp = {}) {
    std::string err;
    auto t = make_transport(k, imp, &err);
    if (!t) throw test::CheckFailure("make_transport failed: " + err);
    if (!t->open(Endpoint::loopback(0), listen, &err)) throw test::CheckFailure("open failed: " + err);
    return t;
}

std::uint64_t now_us() {
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());
}

// Polls every transport (real time) until pred(events of `watch`) is true; collects watch's events.
template <class Pred>
std::vector<TransportEvent> poll_until(std::vector<Transport*> all, Transport* watch, Pred pred, int max_ms = 3000) {
    std::vector<TransportEvent> seen;
    for (int i = 0; i < max_ms; ++i) {
        for (auto* t : all) {
            std::vector<TransportEvent> ev;
            t->poll(now_us(), ev);
            if (t == watch) seen.insert(seen.end(), std::make_move_iterator(ev.begin()), std::make_move_iterator(ev.end()));
        }
        if (pred(seen)) return seen;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return seen;
}

std::vector<std::uint8_t> payload(std::uint8_t tag, std::size_t n = 8) { return std::vector<std::uint8_t>(n, tag); }

std::size_t count(const std::vector<TransportEvent>& ev, TransportEvent::Kind k) {
    return static_cast<std::size_t>(std::count_if(ev.begin(), ev.end(), [&](const TransportEvent& e) { return e.kind == k; }));
}

const TransportEvent* first_message(const std::vector<TransportEvent>& ev) {
    for (const auto& e : ev)
        if (e.kind == TransportEvent::Kind::Message) return &e;
    return nullptr;
}

}  // namespace

TEST(contract_closed_transport_refuses_sends) {
    for (auto k : backends()) {
        auto t = make_open(k, false);
        const auto peer = t->connect(Endpoint::loopback(t->local_endpoint().port));  // any address
        t->close();
        CHECK(!t->is_open());
        CHECK(t->send(peer.value_or(PeerId{1, 0}), payload(1), Delivery::Unreliable, now_us()) == SendResult::Closed);
        CHECK(!t->connect(Endpoint::loopback(1)).has_value());
    }
}

TEST(contract_connected_precedes_first_message_and_bytes_roundtrip) {
    for (auto k : backends()) {
        auto host = make_open(k, true);
        auto client = make_open(k, false);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        CHECK(to_host && to_host->valid());
        CHECK(client->send(*to_host, payload(7, 100), Delivery::Unreliable, now_us()) == SendResult::Sent);
        const auto ev = poll_until({host.get(), client.get()}, host.get(),
                                   [](const auto& e) { return first_message(e) != nullptr; });
        const auto* msg = first_message(ev);
        CHECK(msg && msg->bytes == payload(7, 100));
        CHECK(!ev.empty() && ev.front().kind == TransportEvent::Kind::Connected && ev.front().peer == msg->peer);
        // The reply reaches the client from the host's PeerId.
        CHECK(host->send(msg->peer, payload(8), Delivery::Unreliable, now_us()) == SendResult::Sent);
        const auto back = poll_until({host.get(), client.get()}, client.get(),
                                     [](const auto& e) { return first_message(e) != nullptr; });
        CHECK(first_message(back) && first_message(back)->peer == *to_host);
    }
}

// FB-4-01: a delivery class the backend cannot provide is refused explicitly, never silently downgraded.
TEST(contract_reliable_delivery_is_provided_or_explicitly_unsupported) {
    for (auto k : backends()) {
        auto host = make_open(k, true);
        auto client = make_open(k, false);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        const auto r = client->send(*to_host, payload(1), Delivery::ReliableOrdered, now_us());
        if (client->capabilities().reliable_ordered) {
            CHECK(r == SendResult::Sent);
        } else {
            CHECK(r == SendResult::Unsupported);
            CHECK(client->stats().unsupported == 1);
            CHECK(client->stats().sent == 0);  // nothing went out as an unreliable datagram instead
        }
    }
}

TEST(contract_oversized_send_is_refused) {
    for (auto k : backends()) {
        auto host = make_open(k, true);
        auto client = make_open(k, false);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        const auto max = client->capabilities().max_payload;
        CHECK(max > 0);
        CHECK(client->send(*to_host, payload(1, max), Delivery::Unreliable, now_us()) == SendResult::Sent);
        CHECK(client->send(*to_host, payload(1, max + 1), Delivery::Unreliable, now_us()) == SendResult::TooLarge);
        CHECK(client->stats().too_large == 1);
    }
}

TEST(contract_unknown_peer_is_not_connected) {
    for (auto k : backends()) {
        auto t = make_open(k, true);
        CHECK(t->send(PeerId{}, payload(1), Delivery::Unreliable, now_us()) == SendResult::NotConnected);
        CHECK(t->send(PeerId{999, 0}, payload(1), Delivery::Unreliable, now_us()) == SendResult::NotConnected);
        CHECK(t->stats().not_connected == 2);
    }
}

// FB-4-02: identity is a PeerId, not an address. A peer that goes away and comes back from the SAME address is a
// new PeerId (new generation); the old PeerId is stale and cannot be used.
TEST(contract_endpoint_reuse_gets_new_peer_generation) {
    for (auto k : backends()) {
        auto host = make_open(k, true);
        auto client = make_open(k, false);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        CHECK(client->send(*to_host, payload(1), Delivery::Unreliable, now_us()) == SendResult::Sent);
        auto ev = poll_until({host.get(), client.get()}, host.get(), [](const auto& e) { return first_message(e) != nullptr; });
        const PeerId first = first_message(ev)->peer;
        host->close_peer(first);
        ev = poll_until({host.get(), client.get()}, host.get(),
                        [](const auto& e) { return count(e, TransportEvent::Kind::Disconnected) == 1; });
        CHECK(count(ev, TransportEvent::Kind::Disconnected) == 1 && ev.back().peer == first);
        CHECK(host->send(first, payload(2), Delivery::Unreliable, now_us()) == SendResult::NotConnected);
        CHECK(client->send(*to_host, payload(3), Delivery::Unreliable, now_us()) == SendResult::Sent);  // same address again
        ev = poll_until({host.get(), client.get()}, host.get(), [](const auto& e) { return first_message(e) != nullptr; });
        const PeerId second = first_message(ev)->peer;
        CHECK(second.valid() && second != first && second.id != first.id);
        CHECK(second.generation == first.generation + 1);
        CHECK(count(ev, TransportEvent::Kind::Connected) == 1);
    }
}

TEST(contract_two_simultaneous_peers_are_routed_separately) {
    for (auto k : backends()) {
        auto host = make_open(k, true);
        auto a = make_open(k, false);
        auto b = make_open(k, false);
        const auto pa = a->connect(Endpoint::loopback(host->local_endpoint().port));
        const auto pb = b->connect(Endpoint::loopback(host->local_endpoint().port));
        CHECK(a->send(*pa, payload(0xA), Delivery::Unreliable, now_us()) == SendResult::Sent);
        CHECK(b->send(*pb, payload(0xB), Delivery::Unreliable, now_us()) == SendResult::Sent);
        const auto ev = poll_until({host.get(), a.get(), b.get()}, host.get(),
                                   [](const auto& e) { return count(e, TransportEvent::Kind::Message) == 2; });
        CHECK(count(ev, TransportEvent::Kind::Message) == 2);
        PeerId from_a, from_b;
        for (const auto& e : ev)
            if (e.kind == TransportEvent::Kind::Message) (e.bytes[0] == 0xA ? from_a : from_b) = e.peer;
        CHECK(from_a.valid() && from_b.valid() && from_a != from_b);
        CHECK(host->send(from_b, payload(0xBB), Delivery::Unreliable, now_us()) == SendResult::Sent);
        const auto at_b = poll_until({host.get(), a.get(), b.get()}, b.get(), [](const auto& e) { return first_message(e) != nullptr; });
        CHECK(first_message(at_b) && first_message(at_b)->bytes == payload(0xBB));
        std::vector<TransportEvent> at_a;
        a->poll(now_us(), at_a);
        CHECK(count(at_a, TransportEvent::Kind::Message) == 0);  // not delivered to the wrong peer
    }
}

// FB-4-04: oversized inbound data is surfaced as DroppedInbound and never becomes a Message or a new peer.
TEST(contract_oversized_inbound_is_reported_not_delivered) {
    for (auto k : backends()) {
        if (k != TransportKind::Udp) continue;  // needs a raw sender; other backends reject this at their own layer
        auto host = make_open(k, true);
        UdpSocket raw;
        CHECK(raw.open(Endpoint::loopback(0)));
        CHECK(raw.send_to(Endpoint::loopback(host->local_endpoint().port), std::vector<std::uint8_t>(3000, 0x55)));
        const auto ev = poll_until({host.get()}, host.get(),
                                   [](const auto& e) { return count(e, TransportEvent::Kind::DroppedInbound) == 1; });
        CHECK(count(ev, TransportEvent::Kind::DroppedInbound) == 1);
        CHECK(count(ev, TransportEvent::Kind::Message) == 0);
        CHECK(count(ev, TransportEvent::Kind::Connected) == 0);
        CHECK(host->stats().dropped_inbound_oversized == 1);
    }
}

// Simulated network loss is network behaviour, not a local error: Sent, and counted separately.
TEST(contract_simulated_loss_is_sent_and_counted) {
    for (auto k : backends()) {
        ImpairmentConfig all_lost;
        all_lost.loss = 1.0;
        auto host = make_open(k, true);
        auto client = make_open(k, false, all_lost);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        for (int i = 0; i < 10; ++i) CHECK(client->send(*to_host, payload(1), Delivery::Unreliable, now_us()) == SendResult::Sent);
        CHECK(client->stats().simulated_drops == 10);
        const auto ev = poll_until({host.get(), client.get()}, host.get(), [](const auto&) { return false; }, 100);
        CHECK(count(ev, TransportEvent::Kind::Message) == 0);
    }
}

// FB-4-04: data accepted by send() but still queued at close() is counted, not silently lost.
TEST(contract_queued_data_discarded_on_close_is_counted) {
    for (auto k : backends()) {
        ImpairmentConfig slow;
        slow.latency_ms = 10'000;  // keep it queued
        auto host = make_open(k, true);
        auto client = make_open(k, false, slow);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        CHECK(client->send(*to_host, payload(1), Delivery::Unreliable, now_us()) == SendResult::Sent);
        CHECK(client->send(*to_host, payload(2), Delivery::Unreliable, now_us()) == SendResult::Sent);
        client->close();
        CHECK(client->stats().discarded_on_close == 2);
    }
}
