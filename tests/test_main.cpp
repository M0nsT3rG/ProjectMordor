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
    int passed = 0, failed = 0;
    std::string json = "[";
    for (const auto& t : mordor::test::registry()) {
        if (!filter.empty() && std::string(t.name).find(filter) == std::string::npos) continue;
        const auto start = std::chrono::steady_clock::now();
        std::string error;
        try {
            t.fn();
        } catch (const std::exception& e) {
            error = e.what();
        } catch (...) {
            error = "unknown exception";
        }
        const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
        (error.empty() ? passed : failed)++;
        std::cout << (error.empty() ? "[PASS] " : "[FAIL] ") << t.name << " (" << ms << " ms)";
        if (!error.empty()) std::cout << "\n       " << error;
        std::cout << std::endl;
        std::string esc;
        for (char c : error) esc += (c == '"' || c == '\\') ? std::string("\\") + c : std::string(1, c);
        json += std::string(json.size() > 1 ? "," : "") + "\n  {\"name\":\"" + t.name + "\",\"result\":\"" +
                (error.empty() ? "PASSED" : "FAILED") + "\",\"ms\":" + std::to_string(ms) + ",\"error\":\"" + esc + "\"}";
    }
    json += "\n]\n";
    std::cout << passed << " passed, " << failed << " failed" << std::endl;
    if (!json_path.empty()) std::ofstream(json_path) << json;
    return failed ? 1 : 0;
}
