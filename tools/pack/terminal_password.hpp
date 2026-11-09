// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include "pack_tool.hpp"

namespace mc::pack {

/// Reads passwords from the controlling terminal (/dev/tty) without echoing them, never from
/// arguments, the environment or a pipe. Ctrl+C cancels and leaves the terminal as it was.
class TerminalPasswordSource final : public PasswordSource {
public:
    [[nodiscard]] SecureBuffer read(std::string_view prompt) override;
};

}  // namespace mc::pack
