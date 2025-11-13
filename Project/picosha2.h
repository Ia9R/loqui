/*
 * picosha2: 2014-02-13 (Based on https://github.com/okd/picosha2)
 *
 * This is a header-file-only, SHA256 hash generator in C++
 *
 * Copyright (c) 2014, ok, all rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 * * Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS DAMAGE.
 */

#ifndef PICOSHA2_H
#define PICOSHA2_H
// picosha2:20140213

#include <algorithm>
#include <cassert>
#include <iterator>
#include <sstream>
#include <string>
#include <vector>
#include <iomanip> // <-- SOLUCIÓN: AÑADIDO PARA std::setfill y std::setw

// You can customize SHA256 implementation by defining SHA256_CUSTOM.
// For example, you can use OpenSSL implementation.
// #define SHA256_CUSTOM
// #include <openssl/sha.h>

namespace picosha2 {
#ifdef SHA256_CUSTOM
using ::SHA256_CTX;
using ::SHA256_Init;
using ::SHA256_Update;
using ::SHA256_Final;
#else
// T must be a 32-bit unsigned integer type
template <typename T>
class hash256_one_by_one {
 public:
  hash256_one_by_one() { init(); }

  void init() {
    h_[0] = 0x6a09e667;
    h_[1] = 0xbb67ae85;
    h_[2] = 0x3c6ef372;
    h_[3] = 0xa54ff53a;
    h_[4] = 0x510e527f;
    h_[5] = 0x9b05688c;
    h_[6] = 0x1f83d9ab;
    h_[7] = 0x5be0cd19;

    add_len_ = 0;
    len_ = 0;
  }

  template <typename InIter>
  void process(InIter first, InIter last) {
    add_len_ += static_cast<T>(std::distance(first, last));
    for (; first != last; ++first) {
      buffer_[len_++] = *first;
      if (len_ == 64) {
        transform();
        len_ = 0;
      }
    }
  }

  void finish() {
    buffer_[len_++] = 0x80;

    if (len_ > 56) {
      std::fill(buffer_ + len_, buffer_ + 64, 0x00);
      transform();
      len_ = 0;
    }

    std::fill(buffer_ + len_, buffer_ + 56, 0x00);
    buffer_[63] = static_cast<unsigned char>((add_len_ * 8) & 0xff);
    buffer_[62] = static_cast<unsigned char>(((add_len_ * 8) >> 8) & 0xff);
    buffer_[61] = static_cast<unsigned char>(((add_len_ * 8) >> 16) & 0xff);
    buffer_[60] = static_cast<unsigned char>(((add_len_ * 8) >> 24) & 0xff);
    buffer_[59] = static_cast<unsigned char>(((add_len_ * 8) >> 32) & 0xff);
    buffer_[58] = static_cast<unsigned char>(((add_len_ * 8) >> 40) & 0xff);
    buffer_[57] = static_cast<unsigned char>(((add_len_ * 8) >> 48) & 0xff);
    buffer_[56] = static_cast<unsigned char>(((add_len_ * 8) >> 56) & 0xff);
    transform();
  }

  template <typename OutIter>
  void get_hash_bytes(OutIter first) const {
    for (int i = 0; i < 8; ++i) {
      *first++ = static_cast<unsigned char>((h_[i] >> 24) & 0xff);
      *first++ = static_cast<unsigned char>((h_[i] >> 16) & 0xff);
      *first++ = static_cast<unsigned char>((h_[i] >> 8) & 0xff);
      *first++ = static_cast<unsigned char>(h_[i] & 0xff);
    }
  }

 private:
  void transform() {
    T a = h_[0], b = h_[1], c = h_[2], d = h_[3], e = h_[4], f = h_[5], g = h_[6],
      h = h_[7];

    T w[64];

    for (int i = 0; i < 16; ++i) {
      w[i] = (static_cast<T>(buffer_[i * 4]) << 24) |
             (static_cast<T>(buffer_[i * 4 + 1]) << 16) |
             (static_cast<T>(buffer_[i * 4 + 2]) << 8) |
             (static_cast<T>(buffer_[i * 4 + 3]));
    }

    for (int i = 16; i < 64; ++i) {
      w[i] = s1(w[i - 2]) + w[i - 7] + s0(w[i - 15]) + w[i - 16];
    }

    for (int i = 0; i < 64; ++i) {
      T T1 = h + e1(e) + ch(e, f, g) + k_[i] + w[i];
      T T2 = e0(a) + maj(a, b, c);
      h = g;
      g = f;
      f = e;
      e = d + T1;
      d = c;
      c = b;
      b = a;
      a = T1 + T2;
    }

    h_[0] += a;
    h_[1] += b;
    h_[2] += c;
    h_[3] += d;
    h_[4] += e;
    h_[5] += f;
    h_[6] += g;
    h_[7] += h;
  }

  T s0(T x) { return (x >> 2 | x << 30) ^ (x >> 13 | x << 19) ^ (x >> 22 | x << 10); }

  T s1(T x) { return (x >> 6 | x << 26) ^ (x >> 11 | x << 21) ^ (x >> 25 | x << 7); }

  T e0(T x) { return (x >> 7 | x << 25) ^ (x >> 18 | x << 14) ^ (x >> 3); }

  T e1(T x) { return (x >> 17 | x << 15) ^ (x >> 19 | x << 13) ^ (x >> 10); }

  T ch(T x, T y, T z) { return (x & y) ^ ((~x) & z); }

  T maj(T x, T y, T z) { return (x & y) ^ (x & z) ^ (y & z); }

  static const T k_[64];

  T h_[8];
  unsigned char buffer_[64];
  T len_;
  unsigned long long add_len_;
};

template <typename T>
const T hash256_one_by_one<T>::k_[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1,
    0x923f82a4, 0xab1c5ed5, 0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174, 0xe49b69c1, 0xefbe4786,
    0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147,
    0x06ca6351, 0x14292967, 0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85, 0xa2bfe8a1, 0xa81a664b,
    0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a,
    0x5b9cca4f, 0x682e6ff3, 0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};

typedef hash256_one_by_one<unsigned int> sha256;
#endif

template <typename InIter>
void hash256(InIter first, InIter last,
             std::vector<unsigned char>& hash) {
  hash.resize(32);
#ifdef SHA256_CUSTOM
  SHA256_CTX ctx;
  SHA256_Init(&ctx);
  unsigned long long len = std::distance(first, last);
  if (len > 0) {
    SHA256_Update(&ctx, &*first, len);
  }
  SHA256_Final(hash.data(), &ctx);
#else
  sha256 engine;
  engine.process(first, last);
  engine.finish();
  engine.get_hash_bytes(hash.begin());
#endif
}

template <typename InIter>
void hash256(InIter first, InIter last, std::string& hash) {
  std::vector<unsigned char> hash_v(32);
  hash256(first, last, hash_v);
  hash.clear();
  std::stringstream ss;
  ss << std::hex << std::setfill('0');
  for (unsigned int i = 0; i < 32; ++i) {
    ss << std::setw(2) << static_cast<unsigned int>(hash_v[i]);
  }
  hash = ss.str();
}

template <typename In>
void hash256(In const& in, std::vector<unsigned char>& hash) {
  hash256(in.begin(), in.end(), hash);
}

template <typename In>
void hash256(In const& in, std::string& hash) {
  hash256(in.begin(), in.end(), hash);
}

inline std::string hash256_hex_string(std::string const& in) {
  std::string hash;
  hash256(in, hash);
  return hash;
}

inline std::string hash256_hex_string(std::vector<unsigned char> const& in) {
  std::string hash;
  hash256(in, hash);
  return hash;
}

template <typename InIter>
std::string hash256_hex_string(InIter first, InIter last) {
  std::string hash;
  hash256(first, last, hash);
  return hash;
}
}  // namespace picosha2

#endif  // PICOSHA2_H