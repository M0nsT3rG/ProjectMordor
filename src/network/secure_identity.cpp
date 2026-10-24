#include "network/secure_identity.hpp"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
// windows.h first
#include <aclapi.h>
#include <dpapi.h>
#include <sddl.h>

#include <openssl/crypto.h>
#include <openssl/rand.h>
#include <openssl/evp.h>
#include <openssl/x509.h>

#include <charconv>
#include <cstring>
#include <fstream>
#include <iterator>

#include "network/dtls_session.hpp"

namespace mordor::net {

namespace {

constexpr char kKeyMagic[] = "MRDRKEY1";
constexpr std::size_t kKeyMagicLen = sizeof(kKeyMagic) - 1;
constexpr std::string_view kInvitePrefix = "mrdr1:";

std::wstring widen(const std::string& s) {
    if (s.empty()) return {};
    const int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), w.data(), n);
    return w;
}

// The current user's SID (owned buffer).
std::vector<std::uint8_t> current_user_sid() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_QUERY, &tok)) return {};
    DWORD len = 0;
    GetTokenInformation(tok, TokenUser, nullptr, 0, &len);
    std::vector<std::uint8_t> buf(len);
    std::vector<std::uint8_t> sid;
    if (GetTokenInformation(tok, TokenUser, buf.data(), len, &len)) {
        auto* tu = reinterpret_cast<TOKEN_USER*>(buf.data());
        const DWORD n = GetLengthSid(tu->User.Sid);
        sid.resize(n);
        CopySid(n, sid.data(), tu->User.Sid);
    }
    CloseHandle(tok);
    return sid;
}

bool fail(std::string* error, std::string what) {
    if (error) *error = std::move(what);
    return false;
}

// True if the DACL grants access only to the current user (deny entries are fine; anything unusual is refused).
bool dacl_is_user_only(PACL dacl) {
    const auto me = current_user_sid();
    bool ok = dacl != nullptr && !me.empty();  // a NULL DACL means "everyone": never acceptable
    for (DWORD i = 0; ok && i < dacl->AceCount; ++i) {
        void* ace = nullptr;
        if (!GetAce(dacl, i, &ace)) ok = false;
        else {
            const auto* h = static_cast<ACE_HEADER*>(ace);
            if (h->AceType == ACCESS_ALLOWED_ACE_TYPE) {
                auto* a = static_cast<ACCESS_ALLOWED_ACE*>(ace);
                ok = EqualSid(reinterpret_cast<PSID>(&a->SidStart), const_cast<std::uint8_t*>(me.data())) != FALSE;
            } else if (h->AceType != ACCESS_DENIED_ACE_TYPE) {
                ok = false;
            }
        }
    }
    return ok;
}

// Opens the key file itself (never a reparse point) and checks the DACL on THAT handle, so the checked object is
// the one that is read: no check-then-open race (review #9 FB-9-04). Returns INVALID_HANDLE_VALUE on refusal.
HANDLE open_checked(const std::string& path, std::string* error) {
    HANDLE h = CreateFileW(widen(path).c_str(), GENERIC_READ | READ_CONTROL, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (h == INVALID_HANDLE_VALUE) {
        const DWORD e = GetLastError();
        fail(error, e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND ? "key file not found"
                                                                            : "cannot open key file (" + std::to_string(e) + ")");
        return INVALID_HANDLE_VALUE;
    }
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info) || (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)) {
        CloseHandle(h);
        fail(error, "key file is a symbolic link or other reparse point (refused)");
        return INVALID_HANDLE_VALUE;
    }
    PACL dacl = nullptr;
    PSECURITY_DESCRIPTOR sd = nullptr;
    const bool got = GetSecurityInfo(h, SE_FILE_OBJECT, DACL_SECURITY_INFORMATION, nullptr, nullptr, &dacl, nullptr, &sd) == ERROR_SUCCESS;
    const bool ok = got && dacl_is_user_only(dacl);
    if (sd) LocalFree(sd);
    if (!ok) {
        CloseHandle(h);
        fail(error, got ? "key file permissions grant access to someone other than the current user"
                        : "cannot read the key file permissions");
        return INVALID_HANDLE_VALUE;
    }
    return h;
}

}  // namespace

void PKeyDeleter::operator()(EVP_PKEY* k) const { EVP_PKEY_free(k); }

bool identity_file_is_user_only(const std::string& path, std::string* error) {
    HANDLE h = open_checked(path, error);
    if (h == INVALID_HANDLE_VALUE) return false;
    CloseHandle(h);
    return true;
}

bool create_identity_file(const std::string& path, std::vector<std::uint8_t>* spki, std::string* error) {
    EVP_PKEY* key = EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256");
    if (!key) return fail(error, "key generation failed");
    PKeyPtr owned(key);
    const int der_len = i2d_PrivateKey(key, nullptr);
    std::vector<std::uint8_t> der(static_cast<std::size_t>(der_len > 0 ? der_len : 0));
    unsigned char* p = der.data();
    if (der_len <= 0 || i2d_PrivateKey(key, &p) != der_len) return fail(error, "private key encoding failed");
    DATA_BLOB in{static_cast<DWORD>(der.size()), der.data()}, out{};
    const BOOL protected_ok = CryptProtectData(&in, L"Project Mordor identity", nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out);
    OPENSSL_cleanse(der.data(), der.size());
    if (!protected_ok) return fail(error, "DPAPI CryptProtectData failed (" + std::to_string(GetLastError()) + ")");
    // Protected DACL: only the current user, nothing inherited from the folder.
    const auto me = current_user_sid();
    EXPLICIT_ACCESS_W ea{};
    ea.grfAccessPermissions = GENERIC_ALL;
    ea.grfAccessMode = SET_ACCESS;
    ea.grfInheritance = NO_INHERITANCE;
    ea.Trustee.TrusteeForm = TRUSTEE_IS_SID;
    ea.Trustee.TrusteeType = TRUSTEE_IS_USER;
    ea.Trustee.ptstrName = reinterpret_cast<LPWSTR>(const_cast<std::uint8_t*>(me.data()));
    PACL acl = nullptr;
    SECURITY_DESCRIPTOR sd{};
    SECURITY_ATTRIBUTES sa{sizeof(sa), &sd, FALSE};
    const bool sd_ok = !me.empty() && SetEntriesInAclW(1, &ea, nullptr, &acl) == ERROR_SUCCESS &&
                       InitializeSecurityDescriptor(&sd, SECURITY_DESCRIPTOR_REVISION) &&
                       SetSecurityDescriptorDacl(&sd, TRUE, acl, FALSE) &&
                       SetSecurityDescriptorControl(&sd, SE_DACL_PROTECTED, SE_DACL_PROTECTED);
    // Write a temporary sibling, flush it, then move it into place WITHOUT replacing: an interrupted write never
    // leaves a half key under the final name, and an existing key is never overwritten (review #9 FB-9-04).
    const bool exists = GetFileAttributesW(widen(path).c_str()) != INVALID_FILE_ATTRIBUTES;
    unsigned char rnd[8];
    RAND_bytes(rnd, sizeof rnd);
    char suffix[24];
    std::snprintf(suffix, sizeof suffix, ".tmp-%02x%02x%02x%02x%02x%02x%02x%02x", rnd[0], rnd[1], rnd[2], rnd[3], rnd[4], rnd[5], rnd[6], rnd[7]);
    const std::wstring tmp = widen(path + suffix);
    HANDLE f = sd_ok && !exists ? CreateFileW(tmp.c_str(), GENERIC_WRITE, 0, &sa, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr)
                                : INVALID_HANDLE_VALUE;
    const DWORD create_err = GetLastError();
    bool ok = f != INVALID_HANDLE_VALUE;
    if (ok) {
        DWORD w1 = 0, w2 = 0;
        ok = WriteFile(f, kKeyMagic, static_cast<DWORD>(kKeyMagicLen), &w1, nullptr) && WriteFile(f, out.pbData, out.cbData, &w2, nullptr) &&
             w1 == kKeyMagicLen && w2 == out.cbData && FlushFileBuffers(f);
        CloseHandle(f);
        ok = ok && MoveFileExW(tmp.c_str(), widen(path).c_str(), MOVEFILE_WRITE_THROUGH);  // fails if the target exists
        if (!ok) DeleteFileW(tmp.c_str());  // our own temporary file
    }
    LocalFree(out.pbData);
    if (acl) LocalFree(acl);
    if (!sd_ok) return fail(error, "cannot build a user-only security descriptor");
    if (exists) return fail(error, "key file already exists (refusing to overwrite)");
    if (f == INVALID_HANDLE_VALUE) return fail(error, "cannot create key file (" + std::to_string(create_err) + ")");
    if (!ok) return fail(error, "cannot write key file (or it appeared meanwhile: refusing to overwrite)");
    if (spki) *spki = spki_der(key);
    return true;
}

PKeyPtr load_identity_file(const std::string& path, std::string* error) {
    HANDLE h = open_checked(path, error);  // the same handle is checked and read
    if (h == INVALID_HANDLE_VALUE) return nullptr;
    std::vector<std::uint8_t> raw;
    LARGE_INTEGER size{};
    bool read_ok = GetFileSizeEx(h, &size) && size.QuadPart > 0 && size.QuadPart < (1 << 20);
    if (read_ok) {
        raw.resize(static_cast<std::size_t>(size.QuadPart));
        DWORD got = 0;
        read_ok = ReadFile(h, raw.data(), static_cast<DWORD>(raw.size()), &got, nullptr) && got == raw.size();
    }
    CloseHandle(h);
    if (!read_ok) {
        fail(error, "key file unreadable");
        return nullptr;
    }
    if (raw.size() <= kKeyMagicLen || std::memcmp(raw.data(), kKeyMagic, kKeyMagicLen) != 0) {
        fail(error, "not a Project Mordor key file");
        return nullptr;
    }
    DATA_BLOB blob{static_cast<DWORD>(raw.size() - kKeyMagicLen), raw.data() + kKeyMagicLen}, out{};
    if (!CryptUnprotectData(&blob, nullptr, nullptr, nullptr, nullptr, CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        fail(error, "DPAPI CryptUnprotectData failed (" + std::to_string(GetLastError()) +
                        "): corrupt file, or protected by another Windows user");
        return nullptr;
    }
    const unsigned char* p = out.pbData;
    EVP_PKEY* key = d2i_AutoPrivateKey(nullptr, &p, static_cast<long>(out.cbData));
    OPENSSL_cleanse(out.pbData, out.cbData);
    LocalFree(out.pbData);
    if (!key) fail(error, "key file contents are not a private key");
    return PKeyPtr(key);
}

std::string base64_encode(std::span<const std::uint8_t> bytes) {
    std::string s(4 * ((bytes.size() + 2) / 3), '\0');
    const int n = EVP_EncodeBlock(reinterpret_cast<unsigned char*>(s.data()), bytes.data(), static_cast<int>(bytes.size()));
    s.resize(static_cast<std::size_t>(n));
    return s;
}

std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view text) {
    if (text.empty() || text.size() % 4 != 0) return std::nullopt;
    std::vector<std::uint8_t> out(3 * text.size() / 4);
    const int n = EVP_DecodeBlock(out.data(), reinterpret_cast<const unsigned char*>(text.data()), static_cast<int>(text.size()));
    if (n < 0) return std::nullopt;
    std::size_t pad = 0;
    if (text.back() == '=') ++pad;
    if (text.size() > 1 && text[text.size() - 2] == '=') ++pad;
    out.resize(static_cast<std::size_t>(n) - pad);
    return out;
}

std::string encode_invitation(const Invitation& inv, std::span<const std::uint8_t> host_spki) {
    return std::string(kInvitePrefix) + inv.id + ":" + std::to_string(inv.protocol_version) + ":" +
           std::to_string(inv.expires_unix_s) + ":" + base64_encode(host_spki);
}

std::optional<ParsedInvitation> decode_invitation(std::string_view text, std::string* error) {
    auto bad = [&](const char* why) -> std::optional<ParsedInvitation> {
        fail(error, std::string("invalid invitation: ") + why);
        return std::nullopt;
    };
    if (!text.starts_with(kInvitePrefix)) return bad("unknown format");
    text.remove_prefix(kInvitePrefix.size());
    std::vector<std::string_view> f;
    for (std::size_t pos; (pos = text.find(':')) != std::string_view::npos; text.remove_prefix(pos + 1)) f.push_back(text.substr(0, pos));
    f.push_back(text);
    if (f.size() != 4) return bad("expected 4 fields");
    ParsedInvitation p;
    if (f[0].empty() || f[0].size() > 64) return bad("id length");
    for (char ch : f[0])
        if (!(std::isalnum(static_cast<unsigned char>(ch)) || ch == '-')) return bad("id characters");
    p.invitation.id = std::string(f[0]);
    auto num = [](std::string_view s, auto& v) {
        auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), v);
        return ec == std::errc{} && ptr == s.data() + s.size();
    };
    if (!num(f[1], p.invitation.protocol_version)) return bad("protocol version");
    if (!num(f[2], p.invitation.expires_unix_s)) return bad("expiry");
    auto spki = base64_decode(f[3]);
    if (!spki) return bad("host key encoding");
    const unsigned char* q = spki->data();
    EVP_PKEY* k = d2i_PUBKEY(nullptr, &q, static_cast<long>(spki->size()));
    if (!k) return bad("host key");
    EVP_PKEY_free(k);
    p.host_spki = std::move(*spki);
    return p;
}

}  // namespace mordor::net
