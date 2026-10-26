// mordor-keygen: identity keys and invitations for secure mode (review #8 FB-8-03). Prints PUBLIC data only.
//   mordor-keygen --new FILE                              create a DPAPI-protected, user-only identity key file
//   mordor-keygen --show FILE                             print its public key (base64) and fingerprint
//   mordor-keygen --invite --key FILE --id ID --ttl-s N   print an invitation for the host that owns FILE
#include <ctime>
#include <iostream>

#include "common/cli.hpp"
#include "network/dtls_session.hpp"
#include "network/secure_identity.hpp"

int main(int argc, char** argv) {
    using namespace mordor;
    try {
        cli::Args a(argc, argv);
        std::string err;
        if (a.has("new")) {
            std::vector<std::uint8_t> spki;
            if (!net::create_identity_file(a.str("new", ""), &spki, &err)) throw std::runtime_error(err);
            std::cout << "public: " << net::base64_encode(spki) << "\nfingerprint: " << net::spki_fingerprint(spki) << "\n";
            return 0;
        }
        if (a.has("show")) {
            auto key = net::load_identity_file(a.str("show", ""), &err);
            if (!key) throw std::runtime_error(err);
            const auto spki = net::spki_der(key.get());
            std::cout << "public: " << net::base64_encode(spki) << "\nfingerprint: " << net::spki_fingerprint(spki) << "\n";
            return 0;
        }
        if (a.has("invite")) {
            auto key = net::load_identity_file(a.str("key", ""), &err);
            if (!key) throw std::runtime_error(err);
            const auto ttl = a.num<std::uint64_t>("ttl-s", 3600);
            if (ttl == 0 || ttl > 7 * 24 * 3600) throw std::invalid_argument("--ttl-s must be within 1 s .. 7 days");
            net::Invitation inv{a.str("id", ""), net::kProtocolVersion, static_cast<std::uint64_t>(std::time(nullptr)) + ttl};
            const auto text = net::encode_invitation(inv, net::spki_der(key.get()));
            if (!net::decode_invitation(text, &err)) throw std::invalid_argument(err);  // validates the id as well
            std::cout << text << "\n";
            return 0;
        }
        std::cout << "mordor-keygen --new FILE | --show FILE | --invite --key FILE --id ID --ttl-s N\n";
        return a.has("help") ? 0 : 2;
    } catch (const std::exception& e) {
        std::cerr << "error: " << e.what() << "\n";
        return 1;
    }
}
