// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0
//
// mc_pack_tool: creates the keys that sign rules packs, signs packs and checks them (ADR-06).
// A development tool; see tools/pack/pack_tool.hpp.

#include "pack_tool.hpp"
#include "terminal_password.hpp"

#include <iostream>
#include <span>
#include <string_view>
#include <vector>

int main(int argc, char** argv) {
    try {
        const std::span<char*> raw(argv, static_cast<std::size_t>(argc));
        const std::vector<std::string_view> args(raw.begin() + (raw.empty() ? 0 : 1), raw.end());
        mc::pack::TerminalPasswordSource passwords;
        return mc::pack::run(args, passwords, std::cout, std::cerr);
    } catch (...) {
        std::cerr << "mc_pack_tool: unexpected error\n";
        return 1;
    }
}
