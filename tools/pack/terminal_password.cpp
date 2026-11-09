// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "terminal_password.hpp"

#include <fcntl.h>
#include <termios.h>
#include <unistd.h>

#include <cerrno>
#include <cstddef>

namespace mc::pack {
namespace {

constexpr std::size_t max_password_bytes = 1024;
constexpr unsigned char ctrl_c = 0x03;
constexpr unsigned char ctrl_d = 0x04;

/// The terminal, with echo and signals off until it is destroyed.
class QuietTerminal {
public:
    QuietTerminal()
        : fd_(::open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC)) {  // NOLINT(cppcoreguidelines-pro-type-vararg)
        if (fd_ < 0) {
            throw ToolError("passwords are read from a terminal, and there is none");
        }
        if (::tcgetattr(fd_, &saved_) != 0) {
            ::close(fd_);
            throw ToolError("cannot configure the terminal");
        }
        termios quiet = saved_;
        // Without ISIG, Ctrl+C reaches us as a byte, so the echo is always restored.
        quiet.c_lflag &= ~static_cast<tcflag_t>(ECHO | ISIG);
        quiet.c_lflag |= static_cast<tcflag_t>(ICANON);
        if (::tcsetattr(fd_, TCSAFLUSH, &quiet) != 0) {
            ::close(fd_);
            throw ToolError("cannot configure the terminal");
        }
    }
    QuietTerminal(const QuietTerminal&) = delete;
    QuietTerminal& operator=(const QuietTerminal&) = delete;
    QuietTerminal(QuietTerminal&&) = delete;
    QuietTerminal& operator=(QuietTerminal&&) = delete;
    ~QuietTerminal() {
        ::tcsetattr(fd_, TCSAFLUSH, &saved_);
        write("\n");
        ::close(fd_);
    }

    void write(std::string_view text) const noexcept {
        while (!text.empty()) {
            const ssize_t written = ::write(fd_, text.data(), text.size());
            if (written < 0 && errno == EINTR) {
                continue;
            }
            if (written <= 0) {
                return;
            }
            text.remove_prefix(static_cast<std::size_t>(written));
        }
    }

    /// Reads one byte; returns false at the end of input.
    bool read_byte(unsigned char& byte) const {
        for (;;) {
            const ssize_t got = ::read(fd_, &byte, 1);
            if (got == 1) {
                return true;
            }
            if (got == 0) {
                return false;
            }
            if (errno != EINTR) {
                throw ToolError("cannot read from the terminal");
            }
        }
    }

private:
    int fd_ = -1;
    termios saved_{};
};

}  // namespace

SecureBuffer TerminalPasswordSource::read(std::string_view prompt) {
    SecureBuffer password(max_password_bytes);
    std::size_t length = 0;
    {
        const QuietTerminal terminal;
        terminal.write(prompt);
        unsigned char byte = 0;
        for (;;) {
            if (!terminal.read_byte(byte) || byte == ctrl_d) {
                throw ToolError("no password was entered");
            }
            if (byte == ctrl_c) {
                throw ToolError("cancelled");
            }
            if (byte == '\n' || byte == '\r') {
                break;
            }
            if (length == password.size()) {
                throw ToolError("the password is longer than 1024 bytes");
            }
            password.data()[length++] = byte;
        }
    }
    password.shrink(length);
    return password;
}

}  // namespace mc::pack
