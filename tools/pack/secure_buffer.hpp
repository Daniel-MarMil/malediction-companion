// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace mc::pack {

/// Memory for secrets (passwords, derived keys, secret keys): allocated with sodium_malloc,
/// which surrounds it with guard pages and locks it in RAM when it can, and wiped with
/// sodium_memzero before sodium_free. It can be moved but not copied.
class SecureBuffer {
public:
    /// Allocates size bytes. Throws std::bad_alloc if libsodium cannot.
    explicit SecureBuffer(std::size_t size);
    SecureBuffer(const SecureBuffer&) = delete;
    SecureBuffer& operator=(const SecureBuffer&) = delete;
    SecureBuffer(SecureBuffer&& other) noexcept;
    SecureBuffer& operator=(SecureBuffer&& other) noexcept;
    ~SecureBuffer();

    [[nodiscard]] std::uint8_t* data() noexcept { return data_; }
    [[nodiscard]] const std::uint8_t* data() const noexcept { return data_; }
    [[nodiscard]] std::size_t size() const noexcept { return size_; }
    [[nodiscard]] std::span<std::uint8_t> bytes() noexcept { return {data_, size_}; }
    [[nodiscard]] std::span<const std::uint8_t> bytes() const noexcept { return {data_, size_}; }

    /// Keeps only the first size bytes and wipes the rest.
    void shrink(std::size_t size) noexcept;

private:
    void release() noexcept;

    std::uint8_t* data_ = nullptr;
    std::size_t size_ = 0;
    std::size_t capacity_ = 0;
};

/// Compares two secrets in constant time for a given length.
[[nodiscard]] bool same_secret(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) noexcept;

}  // namespace mc::pack
