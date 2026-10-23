# Test coverage per transport (review #5 FB-5-04)

`SKIPPED` is never `PASSED`. Every skip names the capability that is missing. Where the protocol rule it checks can be expressed on a connection-oriented backend, a substitute test runs on **every** backend. The substitute tests have the `_over_transport` suffix, and their fake peer is a real `Transport` of the backend under test. The DTLS proof (`tests/dtls`) is library-only and is not tied to a transport.

## Skipped on GNS, and what covers the rule instead

| Test (suite) | Missing capability on GNS | Substitute / other coverage |
|---|---|---|
| `client_rejects_stale_snapshots` (integration) | raw UDP fake host | `client_rejects_stale_snapshots_over_transport` |
| `host_survives_invalid_datagrams` (integration) | raw datagrams: GNS validates its own wire format below our layer | `host_survives_invalid_payloads_over_transport` (malformed **payloads** inside a valid GNS connection) |
| `id_exhaustion_rejects_instead_of_wrapping` (integration) | 66 000 connectionless requests; on GNS each request needs its own connection | Not substituted: host-side id logic is transport-independent and covered on UDP |
| `clean_shutdown_notifies_clients_and_releases_socket` (integration) | re-binding a raw UDP socket on the port | Clean shutdown on GNS: `reconnect_gives_up_after_window` and the soak `host_timeouts_match_stalls` gate |
| `input_burst_never_exceeds_one_timestep_per_tick` (integration) | raw UDP fake client | `input_burst_is_shed_over_transport` |
| `steady_inputs_are_never_shed` (integration) | raw UDP fake client | The soak's shed counts on GNS; the shedding logic is transport-independent |
| `wrong_direction_messages_do_not_refresh_liveness` (integration) | raw UDP fake client | `host_ignores_wrong_direction_messages_over_transport` |
| `client_ignores_wrong_direction_messages_for_liveness` (integration) | raw UDP fake host | `client_ignores_wrong_direction_messages_over_transport` |
| `reconnect_token_migrates_slot_to_new_endpoint` (integration) | raw UDP fake clients | `reconnect_token_moves_slot_to_new_connection_over_transport` (GNS adds a stronger guarantee: the old connection is closed) |
| `timed_out_client_is_told_and_reconnects_fast` (integration) | per-instance (asymmetric) impairment | `symmetric_outage_recovers_fast_with_same_identity`; asymmetric cases in separate processes in the soak (client stall) |
| `timeout_notice_is_rate_limited_and_scoped` (integration) | raw UDP fake client | `timeout_notice_is_rate_limited_and_scoped_over_transport` |
| `delayed_timeout_notice_after_reconnect_is_ignored` (integration) | raw UDP fake host | `stale_replayed_and_foreign_session_notices_are_ignored_over_transport` |
| `replayed_and_duplicate_timeout_notices_have_no_extra_effect` (integration) | raw UDP fake host | same |
| `timeout_notice_from_other_session_or_endpoint_is_ignored` (integration) | raw UDP fake host; a stranger injecting into the client's connection | Other session: same substitute. Stranger injection cannot happen on GNS: only the client's own connection can deliver to it (transport property, not our code) |
| `impairment_loss_is_deterministic_and_close_to_configured_rate` (network) | tests `ImpairedSender`, the UDP backend's own impairment | n/a: GNS impairment is GNS's code |
| `impairment_delays_until_due` (network) | same | n/a |
| `timed_out_client_reconnects_with_same_identity` (network) | per-instance impairment (one client cut, the other not) | `repeated_outages_reconnect_each_time_with_same_identity`; soak client stall (separate processes) |
| `silent_host_loss_ends_in_failed_state` (network) | raw UDP silent "host" | `connect_to_absent_host_fails_within_deadline`, `black_holed_connect_hits_overall_deadline` |
| `timeout_notice_recovery_under_jitter_reconnects_once` (network) | per-instance impairment. **Its earlier GNS "pass" was false:** the client's cut silently replaced the host's jitter process-wide, so the scenario never ran. The FB-5-03 guard now refuses that overwrite. | Soak harsh profile (host and client jitter, client stall) on GNS |

## Coverage by area

| Area | UDP | GNS |
|---|---|---|
| Reconnect (token, grace, new connection) | integration + network | integration (`symmetric_outage…`, `reconnect_token…_over_transport`, host restart) + network (`repeated_outages…`) + soak |
| Grace expiry | `reconnect_after_grace_expiry…`, `expired_grace_period…` | same tests (they run on GNS) |
| Connect deadlines and timeout ownership (FB-5-02) | network lifecycle tests | same tests |
| Size limits | `contract_oversized_send_is_refused`, `contract_oversized_inbound…` | same contract tests. `max_payload` is `kMaxPacketSize` on both backends, so GNS fragmentation of larger messages is not exercised (not needed by the protocol today) |
| Reliable ordering, dedup, backlog loss | `Unsupported`, explicitly | contract reliable tests, `contract_mid_backlog…` |
| Malformed data | raw datagrams + payloads | payloads inside a valid connection |
| Error reporting (`SendResult`, counters) | contract tests | contract tests + `contract_process_global_impairment…` |
| Process-global impairment guard (FB-5-03) | n/a (per instance) | `contract_process_global_impairment_is_guarded_and_never_leaks` |
