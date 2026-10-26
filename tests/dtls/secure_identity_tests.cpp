// Review #8 FB-8-03: identity key files (DPAPI + user-only ACL), invitations (public fields only) and the secure-mode
// CLI validation that makes the executables fail closed. Files live in a fresh temp directory removed afterwards.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h first
#include <aclapi.h>
#include <sddl.h>

#include <openssl/evp.h>
#include <openssl/x509.h>

#include <atomic>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <random>

#include "common/cli.hpp"
#include "network/dtls_session.hpp"
#include "network/secure_identity.hpp"
#include "test_support.hpp"

using namespace mordor;
using namespace mordor::net;
namespace fs = std::filesystem;

namespace {

struct TempDir {
    fs::path p;
    TempDir() {
        std::random_device rd;
        p = fs::temp_directory_path() / ("mordor-identity-test-" + std::to_string(GetCurrentProcessId()) + "-" + std::to_string(rd()));
        fs::create_directories(p);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(p, ec);  // our own test files only
    }
    std::string file(const char* name) const { return (p / name).string(); }
};

std::vector<std::uint8_t> read_all(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
}
void write_all(const std::string& path, const std::vector<std::uint8_t>& b) {
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(reinterpret_cast<const char*>(b.data()), static_cast<std::streamsize>(b.size()));
}

cli::Args args(std::vector<const char*> v) {
    v.insert(v.begin(), "mordor-test");
    return cli::Args(static_cast<int>(v.size()), const_cast<char**>(v.data()));
}

bool throws(const std::function<void()>& f, std::string* what = nullptr) {
    try {
        f();
    } catch (const std::exception& e) {
        if (what) *what = e.what();
        return true;
    }
    return false;
}

std::uint64_t now_unix() { return static_cast<std::uint64_t>(std::time(nullptr)); }

}  // namespace

TEST(identity_file_roundtrip_is_dpapi_protected_and_user_only) {
    TempDir d;
    std::vector<std::uint8_t> spki;
    std::string err;
    CHECK(create_identity_file(d.file("host.key"), &spki, &err));
    CHECK(!spki.empty());
    CHECK(identity_file_is_user_only(d.file("host.key"), &err));
    auto key = load_identity_file(d.file("host.key"), &err);
    CHECK(key && spki_der(key.get()) == spki);
    // The file holds no plaintext private key: the DER encoding of the key never appears in it.
    const int n = i2d_PrivateKey(key.get(), nullptr);
    std::vector<std::uint8_t> der(static_cast<std::size_t>(n));
    unsigned char* p = der.data();
    i2d_PrivateKey(key.get(), &p);
    const auto raw = read_all(d.file("host.key"));
    CHECK(std::search(raw.begin(), raw.end(), der.begin() + 7, der.begin() + 7 + 32) == raw.end());  // the scalar
    CHECK(!create_identity_file(d.file("host.key"), nullptr, &err) && err.find("refusing to overwrite") != std::string::npos);
    std::printf("       identity %s... file %zu bytes, user-only DACL, DPAPI-protected\n", spki_fingerprint(spki).substr(0, 16).c_str(), raw.size());
}

TEST(identity_file_failures_are_refused) {
    TempDir d;
    std::string err;
    CHECK(!load_identity_file(d.file("missing.key"), &err) && err == "key file not found");
    CHECK(create_identity_file(d.file("a.key"), nullptr, &err));
    // Corrupt the DPAPI blob.
    auto raw = read_all(d.file("a.key"));
    auto bad = raw;
    bad[bad.size() / 2] ^= 0x5A;
    write_all(d.file("a.key"), bad);
    CHECK(!load_identity_file(d.file("a.key"), &err) && err.find("CryptUnprotectData") != std::string::npos);
    // Not a key file at all.
    write_all(d.file("a.key"), std::vector<std::uint8_t>{'h', 'e', 'l', 'l', 'o', '1', '2', '3', '4'});
    CHECK(!load_identity_file(d.file("a.key"), &err) && err == "not a Project Mordor key file");
    // Permissions broadened to Everyone: refused before reading.
    CHECK(create_identity_file(d.file("b.key"), nullptr, &err));
    PSID everyone = nullptr;
    CHECK(ConvertStringSidToSidW(L"S-1-1-0", &everyone));
    EXPLICIT_ACCESS_W ea{};
    ea.grfAccessPermissions = GENERIC_READ;
    ea.grfAccessMode = GRANT_ACCESS;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_WELL_KNOWN_GROUP;
    ea.Trustee.ptstrName = static_cast<LPWSTR>(everyone);
    PACL old_dacl = nullptr, new_dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    const auto wpath = fs::path(d.file("b.key")).wstring();
    CHECK(GetNamedSecurityInfoW(wpath.c_str(), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &old_dacl, nullptr, &sd) == ERROR_SUCCESS);
    CHECK(SetEntriesInAclW(1, &ea, old_dacl, &new_dacl) == ERROR_SUCCESS);
    CHECK(SetNamedSecurityInfoW(const_cast<LPWSTR>(wpath.c_str()), SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, new_dacl, nullptr) == ERROR_SUCCESS);
    LocalFree(new_dacl);
    LocalFree(sd);
    LocalFree(everyone);
    CHECK(!load_identity_file(d.file("b.key"), &err) && err.find("someone other than the current user") != std::string::npos);
}

TEST(invitation_roundtrip_and_malformed_ones_are_rejected) {
    TempDir d;
    std::vector<std::uint8_t> spki;
    std::string err;
    CHECK(create_identity_file(d.file("h.key"), &spki, &err));
    const Invitation inv{"game-night-1", kProtocolVersion, now_unix() + 600};
    const auto text = encode_invitation(inv, spki);
    CHECK(text.rfind("mrdr1:game-night-1:", 0) == 0);
    auto back = decode_invitation(text, &err);
    CHECK(back && back->invitation == inv && back->host_spki == spki);
    for (std::string bad : {std::string(""), std::string("mrdr2:x:1:2:AAAA"), "mrdr1:" + std::string(65, 'a') + ":1:2:" + base64_encode(spki),
                            "mrdr1:bad id:1:2:" + base64_encode(spki), "mrdr1:ok:one:2:" + base64_encode(spki),
                            "mrdr1:ok:1:soon:" + base64_encode(spki), std::string("mrdr1:ok:1:2:not-base64!"),
                            std::string("mrdr1:ok:1:2:AAAA"), "mrdr1:ok:1:2:" + base64_encode(spki) + ":extra"})
        CHECK(!decode_invitation(bad, &err));
}

TEST(secure_cli_fails_closed_on_every_bad_setup) {
    TempDir d;
    std::vector<std::uint8_t> host_spki, player_spki, other_spki;
    std::string err;
    CHECK(create_identity_file(d.file("host.key"), &host_spki, &err));
    CHECK(create_identity_file(d.file("player.key"), &player_spki, &err));
    CHECK(create_identity_file(d.file("other.key"), &other_spki, &err));
    const auto inv = encode_invitation({"lab-1", kProtocolVersion, now_unix() + 600}, host_spki);
    const auto expired = encode_invitation({"lab-1", kProtocolVersion, now_unix() - 1}, host_spki);
    const auto peer = base64_encode(player_spki);
    const auto hk = d.file("host.key"), pk = d.file("player.key"), ok = d.file("other.key"), missing = d.file("nope.key");
    // Good setups.
    auto h = cli::secure_from(args({"--secure-key", hk.c_str(), "--invitation", inv.c_str(), "--allow-peer", peer.c_str()}), true, now_unix());
    CHECK(h.cfg && h.cfg->pinned_peers.size() == 1 && h.own_fingerprint == spki_fingerprint(host_spki));
    auto c = cli::secure_from(args({"--secure-key", pk.c_str(), "--invitation", inv.c_str()}), false, now_unix());
    CHECK(c.cfg && c.cfg->pinned_peers == std::vector<std::vector<std::uint8_t>>{host_spki});
    CHECK(!cli::secure_from(args({}), true, now_unix()).cfg);  // explicit plaintext only when NO secure option is given
    // Each bad setup throws: the executable exits instead of falling back to plaintext.
    const std::vector<std::pair<const char*, std::function<void()>>> bad = {
        {"key without invitation", [&] { cli::secure_from(args({"--secure-key", pk.c_str()}), false, now_unix()); }},
        {"invitation without key", [&] { cli::secure_from(args({"--invitation", inv.c_str()}), false, now_unix()); }},
        {"host without allowed peers", [&] { cli::secure_from(args({"--secure-key", hk.c_str(), "--invitation", inv.c_str()}), true, now_unix()); }},
        {"client with --allow-peer", [&] { cli::secure_from(args({"--secure-key", pk.c_str(), "--invitation", inv.c_str(), "--allow-peer", peer.c_str()}), false, now_unix()); }},
        {"missing key file", [&] { cli::secure_from(args({"--secure-key", missing.c_str(), "--invitation", inv.c_str()}), false, now_unix()); }},
        {"expired invitation", [&] { cli::secure_from(args({"--secure-key", pk.c_str(), "--invitation", expired.c_str()}), false, now_unix()); }},
        {"invitation for another host key", [&] { cli::secure_from(args({"--secure-key", ok.c_str(), "--invitation", inv.c_str(), "--allow-peer", peer.c_str()}), true, now_unix()); }},
        {"bad allowed-peer key", [&] { cli::secure_from(args({"--secure-key", hk.c_str(), "--invitation", inv.c_str(), "--allow-peer", "%%%"}), true, now_unix()); }},
        {"garbage invitation", [&] { cli::secure_from(args({"--secure-key", pk.c_str(), "--invitation", "hello"}), false, now_unix()); }},
    };
    for (const auto& [name, f] : bad) {
        std::string what;
        CHECK(throws(f, &what));
        std::printf("       refused (%s): %s\n", name, what.c_str());
    }
}

// Review #9 FB-9-04: the loaded object is the checked object; links are refused; writes are atomic.
TEST(identity_file_links_and_interrupted_writes_are_handled) {
    TempDir d;
    std::string err;
    std::vector<std::uint8_t> spki;
    CHECK(create_identity_file(d.file("real.key"), &spki, &err));
    // A symbolic link to a valid key is refused (needs Developer Mode or the symlink privilege to create).
    const auto link = fs::path(d.file("link.key")).wstring(), target = fs::path(d.file("real.key")).wstring();
    if (CreateSymbolicLinkW(link.c_str(), target.c_str(), SYMBOLIC_LINK_FLAG_ALLOW_UNPRIVILEGED_CREATE)) {
        CHECK(!load_identity_file(d.file("link.key"), &err) && err.find("reparse point") != std::string::npos);
        std::printf("       symbolic link to a valid key: refused (%s)\n", err.c_str());
    } else {
        std::printf("       symbolic link check NOT RUN: cannot create symlinks here (error %lu; needs Developer Mode)\n", GetLastError());
    }
    // An interrupted earlier write leaves only a temporary sibling: it neither blocks nor replaces the real key.
    write_all(d.file("new.key") + ".tmp-deadbeefdeadbeef", std::vector<std::uint8_t>{'M', 'R', 'D'});
    CHECK(create_identity_file(d.file("new.key"), nullptr, &err));
    CHECK(load_identity_file(d.file("new.key"), &err) != nullptr);
    // A truncated key under the final name (e.g. copied badly) is refused, and is never silently replaced.
    auto raw = read_all(d.file("new.key"));
    raw.resize(raw.size() / 2);
    write_all(d.file("new.key"), raw);
    CHECK(!load_identity_file(d.file("new.key"), &err));
    CHECK(!create_identity_file(d.file("new.key"), nullptr, &err) && err.find("refusing to overwrite") != std::string::npos);
    // No temporary file of our own survives a successful creation.
    std::size_t tmp = 0;
    for (const auto& e : fs::directory_iterator(d.p)) tmp += e.path().filename().string().find(".tmp-") != std::string::npos;
    CHECK(tmp == 1);  // only the one this test planted
}
