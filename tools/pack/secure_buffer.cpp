// SPDX-FileCopyrightText: 2026 Daniel Martín Millet
// SPDX-License-Identifier: Apache-2.0

#include "secure_buffer.hpp"

#include <sodium.h>

#include <new>
#include <utility>

namespace mc::pack {

SecureBuffer::SecureBuffer(std::size_t size) : size_(size), capacity_(size) {
    if (sodium_init() < 0) {
        throw std::bad_alloc();
    }
    // sodium_malloc(0) may return NULL; one byte keeps the pointer valid.
    data_ = static_cast<std::uint8_t*>(sodium_malloc(size == 0 ? 1 : size));
    if (data_ == nullptr) {
        throw std::bad_alloc();
    }
    sodium_memzero(data_, size == 0 ? 1 : size);
}

SecureBuffer::SecureBuffer(SecureBuffer&& other) noexcept
    : data_(std::exchange(other.data_, nullptr)),
      size_(std::exchange(other.size_, 0)),
      capacity_(std::exchange(other.capacity_, 0)) {}

SecureBuffer& SecureBuffer::operator=(SecureBuffer&& other) noexcept {
    if (this != &other) {
        release();
        data_ = std::exchange(other.data_, nullptr);
        size_ = std::exchange(other.size_, 0);
        capacity_ = std::exchange(other.capacity_, 0);
    }
    return *this;
}

SecureBuffer::~SecureBuffer() {
    release();
}

void SecureBuffer::shrink(std::size_t size) noexcept {
    if (size < size_) {
        sodium_memzero(data_ + size, size_ - size);
        size_ = size;
    }
}

void SecureBuffer::release() noexcept {
    if (data_ != nullptr) {
        sodium_memzero(data_, capacity_ == 0 ? 1 : capacity_);
        sodium_free(data_);
        data_ = nullptr;
    }
    size_ = 0;
    capacity_ = 0;
}

bool same_secret(std::span<const std::uint8_t> a, std::span<const std::uint8_t> b) noexcept {
    return a.size() == b.size() && (a.empty() || sodium_memcmp(a.data(), b.data(), a.size()) == 0);
}

}  // namespace mc::pack
