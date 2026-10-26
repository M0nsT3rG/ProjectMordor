#pragma once
// Identity key files and invitations for the executables (review #8 FB-8-03 / answer 3). GNS/OpenSSL build only.
//  - Key file: "MRDRKEY1" + a DPAPI (CryptProtectData, current-user scope) blob of the DER private key. Created with
//    a PROTECTED DACL granting only the current user; loading refuses a file whose DACL grants anyone else, a
//    corrupt or foreign file, or one another Windows user protected. The private key is never printed, logged,
//    passed on a command line, placed in an invitation or committed.
//  - Invitation: "mrdr1:<id>:<protocol_version>:<expires_unix_s>:<host public key, base64 DER SPKI>". Public fields
//    only. The players confirm the fingerprints (SHA-256 of the SPKI) to each other out of band.
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "network/secure_transport.hpp"

namespace mordor::net {

struct PKeyDeleter {
    void operator()(EVP_PKEY* k) const;
};
using PKeyPtr = std::unique_ptr<EVP_PKEY, PKeyDeleter>;

// Creates a new P-256 identity. Refuses to overwrite an existing file. On success *spki gets the public key.
bool create_identity_file(const std::string& path, std::vector<std::uint8_t>* spki, std::string* error);
PKeyPtr load_identity_file(const std::string& path, std::string* error);
bool identity_file_is_user_only(const std::string& path, std::string* error);

std::string base64_encode(std::span<const std::uint8_t> bytes);
std::optional<std::vector<std::uint8_t>> base64_decode(std::string_view text);

struct ParsedInvitation {
    Invitation invitation;
    std::vector<std::uint8_t> host_spki;
};
std::string encode_invitation(const Invitation& inv, std::span<const std::uint8_t> host_spki);
std::optional<ParsedInvitation> decode_invitation(std::string_view text, std::string* error);

}  // namespace mordor::net
