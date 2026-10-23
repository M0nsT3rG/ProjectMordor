// Backend-independent Transport contract tests (review #4 FB-4-01/02/04). Every test runs against every backend
// available in this build (net::transport_available), so the GNS adapter must pass the same contract.
#include <chrono>
#include <cstring>
#include <set>

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

// Fails the test (instead of crashing on a null dereference) when the expected message never arrived.
const TransportEvent& must_message(const std::vector<TransportEvent>& ev) {
    const auto* m = first_message(ev);
    if (!m) throw test::CheckFailure("expected a Message event but none arrived");
    return *m;
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
        const PeerId first = must_message(ev).peer;
        const Endpoint first_remote = must_message(ev).remote;
        host->close_peer(first);
        ev = poll_until({host.get(), client.get()}, host.get(),
                        [](const auto& e) { return count(e, TransportEvent::Kind::Disconnected) == 1; });
        CHECK(count(ev, TransportEvent::Kind::Disconnected) == 1 && ev.back().peer == first);
        CHECK(host->send(first, payload(2), Delivery::Unreliable, now_us()) == SendResult::NotConnected);
        // The peer comes back. Connectionless backends: the same address simply sends again. Connection-oriented
        // backends: the client is told its connection ended (never left sending into a void), its PeerId is stale
        // too, and it opens a new connection. (Revised after the first GNS run, which showed the original test
        // assumed connectionless reuse of a closed PeerId; see EXP-016.)
        PeerId to_host_again = *to_host;
        if (client->capabilities().reports_disconnects) {
            const auto cev = poll_until({host.get(), client.get()}, client.get(),
                                        [](const auto& e) { return count(e, TransportEvent::Kind::Disconnected) == 1; }, 5000);
            CHECK(count(cev, TransportEvent::Kind::Disconnected) == 1 && cev.back().peer == *to_host);
            CHECK(client->send(*to_host, payload(3), Delivery::Unreliable, now_us()) == SendResult::NotConnected);
            const auto again = client->connect(Endpoint::loopback(host->local_endpoint().port));
            CHECK(again && *again != *to_host);
            to_host_again = *again;
        }
        CHECK(client->send(to_host_again, payload(3), Delivery::Unreliable, now_us()) == SendResult::Sent);
        ev = poll_until({host.get(), client.get()}, host.get(), [](const auto& e) { return first_message(e) != nullptr; });
        const PeerId second = must_message(ev).peer;
        CHECK(second.valid() && second != first && second.id != first.id);
        // Contract: generation counts earlier peers seen at the SAME remote address.
        CHECK(second.generation == (must_message(ev).remote == first_remote ? first.generation + 1 : 0u));
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
        // Exact per-message counting only where impairment runs in our code (see TransportCapabilities).
        if (client->capabilities().exact_loss_accounting) CHECK(client->stats().simulated_drops == 10);
        const auto ev = poll_until({host.get(), client.get()}, host.get(), [](const auto&) { return false; }, 100);
        CHECK(count(ev, TransportEvent::Kind::Message) == 0);
    }
}

// FB-4-04: data accepted by send() but still queued at close() is counted, not silently lost.
TEST(contract_queued_data_discarded_on_close_is_counted) {
    for (auto k : backends()) {
        if (make_open(k, false)->capabilities().exact_loss_accounting) {
            ImpairmentConfig slow;
            slow.latency_ms = 10'000;  // keep it queued in our own impairment queue
            auto host = make_open(k, true);
            auto client = make_open(k, false, slow);
            const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
            CHECK(client->send(*to_host, payload(1), Delivery::Unreliable, now_us()) == SendResult::Sent);
            CHECK(client->send(*to_host, payload(2), Delivery::Unreliable, now_us()) == SendResult::Sent);
            client->close();
            CHECK(client->stats().discarded_on_close == 2);
        } else {
            // Backend-internal queues: reliable data that cannot have been acknowledged is counted per connection.
            ImpairmentConfig cut;
            cut.loss = 1.0;
            auto host = make_open(k, true);
            auto client = make_open(k, false, cut);
            const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
            CHECK(client->send(*to_host, payload(1), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
            CHECK(client->send(*to_host, payload(2), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
            client->close();
            CHECK(client->stats().discarded_on_close == 1);
        }
    }
}

// ---- Reliable-capable backends only (UDP returns Unsupported, asserted above). Each test prints where it ran. ----
namespace {
std::vector<TransportKind> reliable_backends(const char* test) {
    std::vector<TransportKind> out;
    std::string ran, skipped;
    for (auto k : backends()) {
        if (make_open(k, false)->capabilities().reliable_ordered) {
            out.push_back(k);
            ran += std::string(" ") + to_string(k);
        } else {
            skipped += std::string(" ") + to_string(k);
        }
    }
    std::printf("       %s: ran on [%s ] not applicable on [%s ] (no ReliableOrdered)\n", test, ran.c_str(), skipped.c_str());
    return out;
}

std::uint32_t seq_of(const TransportEvent& e) {
    std::uint32_t v = 0;
    if (e.bytes.size() >= 5) std::memcpy(&v, e.bytes.data() + 1, 4);
    return v;
}
std::vector<std::uint8_t> tagged(std::uint8_t tag, std::uint32_t seq) {
    std::vector<std::uint8_t> b(5, tag);
    std::memcpy(b.data() + 1, &seq, 4);
    return b;
}
}  // namespace

TEST(contract_reliable_ordered_exact_under_loss_both_directions) {
    for (auto k : reliable_backends("contract_reliable_ordered_exact_under_loss_both_directions")) {
        ImpairmentConfig bad;
        bad.loss = 0.15;
        bad.latency_ms = 30;
        bad.jitter_ms = 20;
        bad.duplicate = 0.05;
        auto host = make_open(k, true);
        auto client = make_open(k, false, bad);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        constexpr std::uint32_t kN = 500;
        for (std::uint32_t i = 1; i <= kN; ++i) CHECK(client->send(*to_host, tagged(1, i), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
        std::uint32_t expect = 1;
        PeerId from;
        const auto ev = poll_until({host.get(), client.get()}, host.get(), [&](const auto& e) {
            return static_cast<std::uint32_t>(count(e, TransportEvent::Kind::Message)) >= kN;
        }, 15000);
        for (const auto& e : ev) {
            if (e.kind != TransportEvent::Kind::Message) continue;
            CHECK(e.delivery == Delivery::ReliableOrdered);
            CHECK(seq_of(e) == expect);  // exact order, no gaps, no duplicates
            ++expect;
            from = e.peer;
        }
        CHECK(expect == kN + 1);
        // And back to the client.
        for (std::uint32_t i = 1; i <= kN; ++i) CHECK(host->send(from, tagged(1, i), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
        const auto back = poll_until({host.get(), client.get()}, client.get(), [&](const auto& e) {
            return static_cast<std::uint32_t>(count(e, TransportEvent::Kind::Message)) >= kN;
        }, 15000);
        expect = 1;
        for (const auto& e : back)
            if (e.kind == TransportEvent::Kind::Message) CHECK(seq_of(e) == expect++);
        CHECK(expect == kN + 1);
    }
}

TEST(contract_interleaved_reliable_events_and_unreliable_snapshots) {
    for (auto k : reliable_backends("contract_interleaved_reliable_events_and_unreliable_snapshots")) {
        ImpairmentConfig bad;
        bad.loss = 0.10;
        bad.latency_ms = 20;
        auto host = make_open(k, true);
        auto client = make_open(k, false, bad);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        constexpr std::uint32_t kN = 300;
        for (std::uint32_t i = 1; i <= kN; ++i) {
            CHECK(client->send(*to_host, tagged(1, i), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
            CHECK(client->send(*to_host, tagged(2, i), Delivery::Unreliable, now_us()) == SendResult::Sent);
        }
        std::uint32_t next_event = 1;
        std::set<std::uint32_t> snapshots;
        poll_until({host.get(), client.get()}, host.get(), [&](const auto& ev) {
            next_event = 1;
            snapshots.clear();
            for (const auto& e : ev) {
                if (e.kind != TransportEvent::Kind::Message) continue;
                if (e.bytes[0] == 1) {
                    CHECK(e.delivery == Delivery::ReliableOrdered && seq_of(e) == next_event);
                    ++next_event;
                } else {
                    CHECK(e.delivery == Delivery::Unreliable);
                    CHECK(snapshots.insert(seq_of(e)).second);  // unreliable may drop, never duplicates at app level
                }
            }
            return next_event == kN + 1;
        }, 15000);
        CHECK(next_event == kN + 1);
        CHECK(snapshots.size() <= kN);
        std::printf("       %s: events %u/%u exact; snapshots %zu/%u\n", to_string(k), kN, kN, snapshots.size(), kN);
    }
}

TEST(contract_backpressure_is_reported_and_accepted_reliable_is_delivered) {
    for (auto k : reliable_backends("contract_backpressure_is_reported_and_accepted_reliable_is_delivered")) {
        auto host = make_open(k, true);
        auto client = make_open(k, false);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        std::uint32_t accepted = 0, refused = 0;
        for (int i = 0; i < 300'000 && refused == 0; ++i) {
            const auto r = client->send(*to_host, tagged(1, accepted + 1), Delivery::ReliableOrdered, now_us());
            if (r == SendResult::Sent) ++accepted;
            else if (r == SendResult::Backpressure) ++refused;
            else CHECK(r == SendResult::Sent);  // any other result is a contract violation here
        }
        CHECK(refused == 1);  // the bounded queue pushed back explicitly
        CHECK(client->stats().backpressure == 1);
        std::uint32_t expect = 1;
        poll_until({host.get(), client.get()}, host.get(), [&](const auto& ev) {
            expect = 1;
            for (const auto& e : ev)
                if (e.kind == TransportEvent::Kind::Message) {
                    CHECK(seq_of(e) == expect);
                    ++expect;
                }
            return expect == accepted + 1;
        }, 30000);
        CHECK(expect == accepted + 1);  // everything accepted before the pushback arrived, in order
        std::printf("       %s: accepted %u reliable messages before backpressure; all delivered in order\n", to_string(k), accepted);
    }
}

// Disconnection with pending reliable messages must be reported, never silent.
TEST(contract_disconnect_with_pending_reliable_is_reported) {
    for (auto k : reliable_backends("contract_disconnect_with_pending_reliable_is_reported")) {
        auto host = make_open(k, true);
        auto client = make_open(k, false);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        CHECK(client->send(*to_host, tagged(1, 1), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
        poll_until({host.get(), client.get()}, host.get(), [](const auto& e) { return first_message(e) != nullptr; });
        ImpairmentConfig cut;
        cut.loss = 1.0;  // nothing gets through any more: pending reliable data can never be acknowledged
        client->set_impairment(cut);
        for (std::uint32_t i = 2; i <= 6; ++i) CHECK(client->send(*to_host, tagged(1, i), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
        const auto ev = poll_until({host.get(), client.get()}, client.get(),
                                   [](const auto& e) { return count(e, TransportEvent::Kind::Disconnected) == 1; }, 15000);
        CHECK(count(ev, TransportEvent::Kind::Disconnected) == 1);  // the transport detected the loss itself
        CHECK(client->stats().accepted_then_failed == 1);         // and said that accepted reliable data was lost
        client->set_impairment({});
    }
}

TEST(contract_close_peer_with_unacked_reliable_is_counted) {
    for (auto k : reliable_backends("contract_close_peer_with_unacked_reliable_is_counted")) {
        auto host = make_open(k, true);
        auto client = make_open(k, false);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        CHECK(client->send(*to_host, tagged(1, 1), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
        poll_until({host.get(), client.get()}, host.get(), [](const auto& e) { return first_message(e) != nullptr; });
        ImpairmentConfig cut;
        cut.loss = 1.0;
        client->set_impairment(cut);
        CHECK(client->send(*to_host, tagged(1, 2), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
        client->close_peer(*to_host);
        CHECK(client->stats().closed_with_unacked_reliable == 1);
        CHECK(client->send(*to_host, tagged(1, 3), Delivery::ReliableOrdered, now_us()) == SendResult::NotConnected);
        client->set_impairment({});
    }
}
