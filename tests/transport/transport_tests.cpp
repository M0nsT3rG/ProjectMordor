// Backend-independent Transport contract tests (review #4 FB-4-01/02/04). Every test runs against every backend
// available in this build (net::transport_available), so the GNS adapter must pass the same contract.
#include <algorithm>
#include <chrono>
#include <cstring>
#include <optional>
#include <set>
#include <thread>

#include "network/transport.hpp"
#include "network/udp_transport.hpp"
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

// Codex review of the combined diff (P1): datagrams from many new addresses must not grow the UDP backend's
// per-source state without bound. The table never exceeds max_peers, a peer that keeps sending is never evicted,
// idle peers expire, and the generation history stays bounded. (UDP-backend specific: GNS owns its connections.)
TEST(udp_peer_table_is_bounded_and_idle_peers_expire) {
    UdpTransport host(ImpairmentConfig{});
    std::string err;
    CHECK(host.open(Endpoint::loopback(0), true, &err));
    host.set_peer_limits({.max_peers = 8, .idle_expiry_us = 2'000'000});
    const auto to = Endpoint::loopback(host.local_endpoint().port);
    auto active = make_open(TransportKind::Udp, false);
    const auto active_peer = active->connect(to);
    std::vector<std::unique_ptr<Transport>> strangers;
    for (int i = 0; i < 20; ++i) strangers.push_back(make_open(TransportKind::Udp, false));
    std::uint64_t t = 1'000'000;
    std::vector<TransportEvent> seen;
    std::size_t max_table = 0;
    auto tick = [&](bool strangers_send, int n) {
        for (int i = 0; i < n; ++i) {
            CHECK(active->send(*active_peer, payload(1), Delivery::Unreliable, t) == SendResult::Sent);
            if (strangers_send && i == 0)
                for (auto& s : strangers) s->send(*s->connect(to), payload(2), Delivery::Unreliable, t);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
            std::vector<TransportEvent> ev;
            host.poll(t, ev);
            seen.insert(seen.end(), ev.begin(), ev.end());
            max_table = std::max(max_table, host.peer_count());
            t += 50'000;
        }
    };
    tick(false, 10);  // the player is established first (many datagrams), as players are before a flood
    tick(true, 10);   // then 20 strangers, one datagram each, against a table of 8
    CHECK(max_table <= 8);
    std::optional<PeerId> player;
    for (const auto& e : seen)
        if (e.kind == TransportEvent::Kind::Message && e.bytes == payload(1)) player = e.peer;
    CHECK(player.has_value());
    auto disconnected = [&](std::string_view why, std::optional<PeerId> who = std::nullopt) {
        return std::count_if(seen.begin(), seen.end(), [&](const auto& e) {
            return e.kind == TransportEvent::Kind::Disconnected && e.reason == why && (!who || e.peer == *who);
        });
    };
    CHECK(disconnected("evicted_table_full") >= 13);
    CHECK(disconnected("evicted_table_full", player) == 0);  // most datagrams exchanged: never the one evicted
    tick(false, 60);  // 3 s more: only the player keeps sending, everyone else goes idle past 2 s
    CHECK(disconnected("idle_expired") >= 1 && disconnected("idle_expired", player) == 0);
    CHECK(host.peer_count() == 1);
    CHECK(host.generation_entries() <= 8);
    std::printf("       table max %zu of 8, evicted %lld, expired %lld, generation entries %zu\n", max_table,
                static_cast<long long>(disconnected("evicted_table_full")), static_cast<long long>(disconnected("idle_expired")),
                host.generation_entries());
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
            // Backend-internal queues: reliable data that cannot have been acknowledged is counted per CONNECTION,
            // in closed_with_unacked_reliable; discarded_on_close is per message and stays 0 here (review #5 FB-5-01:
            // one counter must not mean messages on one backend and connections on another).
            ImpairmentConfig cut;
            cut.loss = 1.0;
            auto host = make_open(k, true);
            auto client = make_open(k, false, cut);
            const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
            CHECK(client->send(*to_host, payload(1), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
            CHECK(client->send(*to_host, payload(2), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
            client->close();
            CHECK(client->stats().closed_with_unacked_reliable == 1);
            CHECK(client->stats().discarded_on_close == 0);
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

// Review #5 FB-5-01: a connection lost in the middle of a reliable backlog. The loss is reported, the transport
// never replays the old backlog onto the new connection (replay is the application's job, with its own ids),
// and the new connection delivers its own messages exactly and in order.
TEST(contract_mid_backlog_disconnect_then_reconnect_is_clean) {
    for (auto k : reliable_backends("contract_mid_backlog_disconnect_then_reconnect_is_clean")) {
        auto host = make_open(k, true);
        auto client = make_open(k, false);
        const auto host_ep = Endpoint::loopback(host->local_endpoint().port);
        const auto old_peer = client->connect(host_ep);
        for (std::uint32_t i = 1; i <= 50; ++i) CHECK(client->send(*old_peer, tagged(1, i), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
        std::uint32_t old_seen = 0;
        poll_until({host.get(), client.get()}, host.get(), [&](const auto& ev) {
            old_seen = 0;
            for (const auto& e : ev)
                if (e.kind == TransportEvent::Kind::Message) {
                    CHECK(seq_of(e) == old_seen + 1);  // in order up to the cut
                    ++old_seen;
                }
            return old_seen == 50;
        });
        CHECK(old_seen == 50);
        ImpairmentConfig cut;
        cut.loss = 1.0;
        CHECK(client->set_impairment(cut));
        for (std::uint32_t i = 51; i <= 100; ++i) CHECK(client->send(*old_peer, tagged(1, i), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
        const auto lost = poll_until({host.get(), client.get()}, client.get(),
                                     [](const auto& e) { return count(e, TransportEvent::Kind::Disconnected) == 1; }, 15000);
        CHECK(count(lost, TransportEvent::Kind::Disconnected) == 1);
        CHECK(client->stats().accepted_then_failed == 1);  // the unacknowledged backlog was reported, not silent
        CHECK(client->set_impairment({}));
        const auto new_peer = client->connect(host_ep);
        CHECK(new_peer.has_value() && *new_peer != *old_peer);
        for (std::uint32_t i = 1; i <= 20; ++i) CHECK(client->send(*new_peer, tagged(2, i), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
        std::uint32_t fresh = 0, stale = 0;
        poll_until({host.get(), client.get()}, host.get(), [&](const auto& ev) {
            fresh = stale = 0;
            for (const auto& e : ev) {
                if (e.kind != TransportEvent::Kind::Message) continue;
                if (e.bytes[0] == 1) ++stale;  // backlog of the dead connection
                else {
                    CHECK(seq_of(e) == fresh + 1);
                    ++fresh;
                }
            }
            return fresh == 20;
        }, 10000);
        CHECK(fresh == 20);
        CHECK(stale == 0);
        std::printf("       %s: 50 delivered before the cut, 50 reported lost, 20 delivered in order on the new connection\n", to_string(k));
    }
}

// Review #5 FB-5-03: where impairment is process-global there is one effective config. A different request is
// refused explicitly, the same request joins, and the config ends only when its last holder lets go: closing one
// holder must not end another's simulation, and nothing leaks after the last.
TEST(contract_process_global_impairment_is_guarded_and_never_leaks) {
    for (auto k : backends()) {
        auto a = make_open(k, false);
        auto b = make_open(k, false);
        ImpairmentConfig cut;
        cut.loss = 1.0;
        ImpairmentConfig slow;
        slow.latency_ms = 50;
        CHECK(a->set_impairment(cut));
        if (a->capabilities().per_instance_impairment) {
            CHECK(b->set_impairment(slow));  // independent per instance
            std::printf("       %s: per-instance impairment, no guard needed\n", to_string(k));
            continue;
        }
        // An observer pair, unimpaired itself, sees the effective global config.
        auto x = make_open(k, true);
        auto y = make_open(k, false);
        CHECK(a->set_impairment({}));  // let the observers connect first
        const auto to_x = y->connect(Endpoint::loopback(x->local_endpoint().port));
        CHECK(y->send(*to_x, tagged(9, 1), Delivery::ReliableOrdered, now_us()) == SendResult::Sent);
        CHECK(must_message(poll_until({x.get(), y.get()}, x.get(), [](const auto& e) { return first_message(e) != nullptr; })).bytes[0] == 9);
        auto delivered = [&](std::uint32_t seq, int ms) {
            CHECK(y->send(*to_x, tagged(7, seq), Delivery::Unreliable, now_us()) == SendResult::Sent);
            const auto ev = poll_until({x.get(), y.get()}, x.get(), [&](const auto& e) {
                for (const auto& m : e)
                    if (m.kind == TransportEvent::Kind::Message && m.bytes[0] == 7 && seq_of(m) == seq) return true;
                return false;
            }, ms);
            for (const auto& m : ev)
                if (m.kind == TransportEvent::Kind::Message && m.bytes[0] == 7 && seq_of(m) == seq) return true;
            return false;
        };
        CHECK(a->set_impairment(cut));
        CHECK(!b->set_impairment(slow));            // different config: refused, explicitly
        CHECK(b->stats().impairment_refused == 1);
        std::string err;
        auto c = make_transport(k, slow, &err);     // opening with a conflicting config fails, it does not override
        CHECK(c && !c->open(Endpoint::loopback(0), false, &err) && !err.empty());
        CHECK(b->set_impairment(cut));              // same config: joins
        CHECK(!delivered(1, 300));                  // cut is in effect
        a->close();                                 // one holder leaves...
        CHECK(!delivered(2, 300));                  // ...the other's simulation is unchanged
        CHECK(b->set_impairment({}));               // last holder lets go
        CHECK(delivered(3, 2000));                  // nothing leaks
        std::printf("       %s: conflicting config refused, same config shared, released by the last holder\n", to_string(k));
    }
}

// EXP-021: messages that reached the receiver before the sender closed are delivered before Disconnected,
// never dropped with the connection.
TEST(contract_messages_received_before_disconnect_are_delivered) {
    for (auto k : reliable_backends("contract_messages_received_before_disconnect_are_delivered")) {
        auto host = make_open(k, true);
        auto client = make_open(k, false);
        const auto to_host = client->connect(Endpoint::loopback(host->local_endpoint().port));
        poll_until({host.get(), client.get()}, client.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; });
        poll_until({host.get(), client.get()}, host.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; }, 500);
        for (std::uint32_t i = 1; i <= 20; ++i) CHECK(client->send(*to_host, tagged(1, i), Delivery::Unreliable, now_us()) == SendResult::Sent);
        std::this_thread::sleep_for(std::chrono::milliseconds(100));  // they reach the host, which is not polled
        client->close();
        std::this_thread::sleep_for(std::chrono::milliseconds(200));  // the close notification reaches it too
        const auto ev = poll_until({host.get()}, host.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Disconnected) == 1; }, 5000);
        std::size_t msgs = 0, disc_at = SIZE_MAX;
        for (std::size_t i = 0; i < ev.size(); ++i) {
            if (ev[i].kind == TransportEvent::Kind::Message) {
                ++msgs;
                CHECK(disc_at == SIZE_MAX);  // never after the Disconnected
            }
            if (ev[i].kind == TransportEvent::Kind::Disconnected) disc_at = i;
        }
        std::printf("       %s: %zu of 20 messages delivered before Disconnected\n", to_string(k), msgs);
        CHECK(disc_at != SIZE_MAX && msgs == 20);  // loopback, no impairment: all of them
    }
}

// ---- KI-27 / review #6 FB-6-03: GNS internal assertion during teardown. Each scenario is repeated and the
// assertions GNS reports are counted per scenario, so the trigger can be isolated (and a fix regression-tested).
#ifdef MORDOR_HAVE_GNS
#include "network/gns_transport.hpp"

std::unique_ptr<Transport> g_keeper;  // the "runtime kept alive" scenario's extra instance (was a function static)

TEST(gns_teardown_assertion_probe) {
    constexpr int kRuns = 200;
    struct Scenario {
        const char* name;
        void (*run)();
    };
    const Scenario scenarios[] = {
        {"open_close_client", [] { auto c = make_open(TransportKind::Gns, false); }},
        {"open_close_listener", [] { auto h = make_open(TransportKind::Gns, true); }},
        {"open_listener_wait_50ms_close", [] {
             auto h = make_open(TransportKind::Gns, true);
             std::this_thread::sleep_for(std::chrono::milliseconds(50));
         }},
        {"listener_with_runtime_kept_alive", [] {  // another instance holds the runtime: no Init/Kill per run
             if (!g_keeper) g_keeper = make_open(TransportKind::Gns, false);
             auto h = make_open(TransportKind::Gns, true);
         }},
        {"connect_then_close_immediately", [] {
             auto h = make_open(TransportKind::Gns, true);
             auto c = make_open(TransportKind::Gns, false);
             const auto p = c->connect(Endpoint::loopback(h->local_endpoint().port));
             c->send(*p, payload(1), Delivery::Unreliable, now_us());
         }},
        {"connected_close_client_first", [] {
             auto h = make_open(TransportKind::Gns, true);
             auto c = make_open(TransportKind::Gns, false);
             c->connect(Endpoint::loopback(h->local_endpoint().port));
             poll_until({h.get(), c.get()}, c.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; });
             c.reset();
         }},
        {"connected_close_host_first", [] {
             auto h = make_open(TransportKind::Gns, true);
             auto c = make_open(TransportKind::Gns, false);
             c->connect(Endpoint::loopback(h->local_endpoint().port));
             poll_until({h.get(), c.get()}, c.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; });
             h.reset();
         }},
    };
    std::uint64_t total = 0;
    for (const auto& s : scenarios) {
        const auto before = gns_internal_assertions();
        for (int i = 0; i < kRuns; ++i) s.run();
        const auto n = gns_internal_assertions() - before;
        total += n;
        std::printf("       KI-27 probe %-32s %llu assertions in %d runs\n", s.name, static_cast<unsigned long long>(n), kRuns);
    }
    g_keeper.reset();  // released before exit: the runner shuts GNS down only when nothing is open (KI-40)
    std::printf("       KI-27 probe total: %llu\n", static_cast<unsigned long long>(total));
    CHECK(total == 0);  // regression gate for the KI-27 mitigation (process-lifetime runtime + settle after Init)
}

// Review #12 FB-12-02 (KI-40): what the runtime holds returns to its earlier level after orderly closes, for one
// instance and for many in sequence, whichever side closes first; nothing stays registered for exit to tear down.
TEST(gns_lifecycle_returns_to_baseline_after_orderly_and_repeated_use) {
    const auto base = gns_lifecycle();
    auto pair_once = [](bool host_first) {
        auto h = make_open(TransportKind::Gns, true);
        auto c = make_open(TransportKind::Gns, false);
        c->connect(Endpoint::loopback(h->local_endpoint().port));
        poll_until({h.get(), c.get()}, h.get(), [](const auto& e) { return count(e, TransportEvent::Kind::Connected) == 1; });
        const auto during = gns_lifecycle();
        if (host_first) h.reset(); else c.reset();
        return during;
    };
    const auto during = pair_once(false);
    CHECK(during.listen_sockets == base.listen_sockets + 1 && during.connections == base.connections + 2);
    for (int i = 0; i < 20; ++i) pair_once(i % 2 == 0);
    const auto after = gns_lifecycle();
    std::printf("       GNS lifecycle: base %zu connections / %zu listen sockets, during %zu / %zu, after 21 pairs %zu / %zu (refs %d -> %d)\n",
                base.connections, base.listen_sockets, during.connections, during.listen_sockets, after.connections,
                after.listen_sockets, base.refs, after.refs);
    CHECK(after.connections == base.connections && after.listen_sockets == base.listen_sockets && after.refs == base.refs);
    CHECK(after.initialized);  // by design the runtime lives for the process (KI-27); never killed by us
}

// Review #15 FB-15-02 (KI-42): a closed GNS connection must ALWAYS be reported to the other side. Stress the
// endpoint-reuse pattern with three teardown orders and require every Disconnected, and no status callback that no
// transport claimed. MORDOR_GNS_TRACE=1 prints the lifecycle of each connection when this needs diagnosing.
TEST(gns_close_is_always_reported_to_the_other_side) {
    struct Order { const char* name; bool host_closes, client_closes; };
    const Order orders[] = {{"host closes", true, false}, {"client closes", false, true}, {"both close", true, true}};
    const auto unrouted_before = gns_unrouted_status_callbacks();
    auto host = make_open(TransportKind::Gns, true);
    auto client = make_open(TransportKind::Gns, false);
    const auto host_ep = Endpoint::loopback(host->local_endpoint().port);
    int misses = 0, rounds = 0;
    for (const auto& o : orders) {
        int order_misses = 0;
        for (int i = 0; i < 60; ++i) {
            ++rounds;
            const auto to_host = client->connect(host_ep);
            CHECK(client->send(*to_host, payload(1), Delivery::Unreliable, now_us()) == SendResult::Sent);
            auto ev = poll_until({host.get(), client.get()}, host.get(), [](const auto& e) { return first_message(e) != nullptr; });
            const PeerId at_host = must_message(ev).peer;
            if (o.host_closes) host->close_peer(at_host);
            if (o.client_closes) client->close_peer(*to_host);
            // Whoever did NOT close must hear about it; whoever closed gets no event (its PeerId is simply gone).
            if (!o.client_closes) {
                const auto cev = poll_until({host.get(), client.get()}, client.get(),
                                            [](const auto& e) { return count(e, TransportEvent::Kind::Disconnected) == 1; }, 5000);
                if (count(cev, TransportEvent::Kind::Disconnected) != 1) ++order_misses;
            }
            if (!o.host_closes) {
                const auto hev = poll_until({host.get(), client.get()}, host.get(),
                                            [](const auto& e) { return count(e, TransportEvent::Kind::Disconnected) == 1; }, 5000);
                if (count(hev, TransportEvent::Kind::Disconnected) != 1) ++order_misses;
            }
            if (o.host_closes && o.client_closes) poll_until({host.get(), client.get()}, host.get(), [](const auto&) { return false; }, 20);
        }
        std::printf("       KI-42 %-13s: %d missed Disconnected in 60 rounds\n", o.name, order_misses);
        misses += order_misses;
    }
    const auto unrouted = gns_unrouted_status_callbacks() - unrouted_before;
    std::printf("       KI-42 total: %d misses in %d rounds; %llu status callbacks claimed by no transport\n", misses, rounds,
                static_cast<unsigned long long>(unrouted));
    CHECK(misses == 0);
}

// Registered last in this file: no GNS internal assertion anywhere in this process's contract run.
TEST(gns_reported_no_internal_assertions_in_this_run) {
    std::printf("       GNS internal assertions in this process: %llu\n", static_cast<unsigned long long>(gns_internal_assertions()));
    CHECK(gns_internal_assertions() == 0);
}

// KI-40 fix (review #15): the exit shutdown is refused while a transport is open, works when none is, and afterwards
// nothing can re-initialise GNS in this process (KI-27). MUST stay the very last test: GNS is gone after it.
TEST(gns_shutdown_for_exit_refuses_while_open_and_blocks_reinit) {
    {
        auto open_one = make_open(TransportKind::Gns, false);
        CHECK(!gns_shutdown_for_exit());  // refused: a transport is still open
        CHECK(gns_lifecycle().initialized);
    }
    CHECK(gns_shutdown_for_exit());
    CHECK(!gns_lifecycle().initialized);
    std::string err;
    auto late = make_transport(TransportKind::Gns, {}, &err);
    CHECK(late && !late->open(Endpoint::loopback(0), false, &err));  // no Kill-then-Init in one process
    std::printf("       after the exit shutdown, a new GNS transport is refused: %s\n", err.c_str());
    CHECK(gns_shutdown_for_exit());  // idempotent: the runner calls it again at exit
}
#endif
