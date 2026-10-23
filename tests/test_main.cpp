// Shared runner: `<suite> [name-filter] [--json results.json]`. Exit code 1 if any test fails.
#include <chrono>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iostream>
#include <string>

#include "test_support.hpp"

int main(int argc, char** argv) {
    std::string filter, json_path;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "--json" && i + 1 < argc) json_path = argv[++i];
        else filter = a;
    }
    int passed = 0, failed = 0, skipped = 0;
    std::string json = "[";
    for (const auto& t : mordor::test::registry()) {
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) continue;
        const auto start = std::chrono::steady_clock::now();
        std::string error, skip_reason;
        try {
            t.fn();
        } catch (const mordor::test::SkipTest& s) {
            skip_reason = s.what();
        } catch (const std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "unknown exception";
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        const char* result = !skip_reason.empty() ? "SKIPPED" : error.empty() ? "PASSED" : "FAILED";
        (!skip_reason.empty() ? skipped : error.empty() ? passed : failed)++;
        std::cout << (!skip_reason.empty() ? "[SKIP] " : error.empty() ? "[PASS] " : "[FAIL] ") << t.name << " (" << ms << " ms)";
        if (!error.empty()) std::cout << "\n       " << error;
        if (!skip_reason.empty()) std::cout << "\n       " << skip_reason;
        std::cout << std::endl;
        std::string esc;
        for (char c : (error.empty() ? skip_reason : error)) esc += (c == '"' || c == '\\') ? std::string("\\") + c : std::string(1, c);
        json += std::string(json.size() > 1 ? "," : "") + "\n  {\"name\":\"" + t.name + "\",\"result\":\"" + result +
                "\",\"transport\":\"" + mordor::net::to_string(mordor::test::test_transport()) + "\",\"ms\":" + std::to_string(ms) +
                ",\"error\":\"" + esc + "\"}";
    }
    json += "\n]\n";
    std::cout << passed << " passed, " << failed << " failed, " << skipped << " skipped (transport "
              << mordor::net::to_string(mordor::test::test_transport()) << ")" << std::endl;
    if (!json_path.empty()) std::ofstream(json_path) << json;
    return failed ? 1 : 0;
}
