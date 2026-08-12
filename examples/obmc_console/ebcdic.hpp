#pragma once
/**
 * @file ebcdic.hpp
 * @brief EBCDIC Code Page 037 → ASCII conversion table
 *
 * IBM i (AS/400) 5250 data streams encode character data in EBCDIC CP037.
 * This header provides a compile-time lookup table and a small inline
 * conversion function used by the 5250 stream parser.
 *
 * Non-printable / unmapped EBCDIC bytes are returned as ' ' (space) so they
 * are harmlessly rendered as blanks rather than terminal control noise.
 */

#include <cstdint>

namespace NSNAME
{

// IBM EBCDIC Code Page 037 → ASCII (7-bit printable)
// Index = EBCDIC byte value (0x00–0xFF)
// Value = ASCII character to display
//
// Note: '[' and ']' must be written as integer casts inside a char array
// initialiser to avoid the compiler treating them as array-subscript tokens.
// clang-format off
inline constexpr char kEbcdicToAsciiTable[256] = {
/*00*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
/*10*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
/*20*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
/*30*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
/*40*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',
       char(0x5B), /* [ */  '.', '<', '(', '+', '|',
/*50*/ '&',' ',' ',' ',' ',' ',' ',' ',' ',
       char(0x5D), /* ] */  '$', '*', ')', ';', '^',
/*60*/ '-', '/', ' ',' ',' ',' ',' ',' ',' ',' ',' ', ',', '%', '_', '>', '?',
/*70*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ', ':', '#', '@','\'', '=', '"',
/*80*/ ' ', 'a', 'b', 'c', 'd', 'e', 'f', 'g', 'h', 'i',' ',' ',' ',' ',' ',' ',
/*90*/ ' ', 'j', 'k', 'l', 'm', 'n', 'o', 'p', 'q', 'r',' ',' ',' ',' ',' ',' ',
/*A0*/ ' ', '~', 's', 't', 'u', 'v', 'w', 'x', 'y', 'z',' ',' ',' ',' ',' ',' ',
/*B0*/ ' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',' ',
/*C0*/ '{', 'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I',' ',' ',' ',' ',' ',' ',
/*D0*/ '}', 'J', 'K', 'L', 'M', 'N', 'O', 'P', 'Q', 'R',' ',' ',' ',' ',' ',' ',
/*E0*/ '\\', ' ', 'S', 'T', 'U', 'V', 'W', 'X', 'Y', 'Z',' ',' ',' ',' ',' ',' ',
/*F0*/ '0', '1', '2', '3', '4', '5', '6', '7', '8', '9',' ',' ',' ',' ',' ',' ',
};
// clang-format on

/// Convert a single EBCDIC CP037 byte to its printable ASCII equivalent.
/// Returns ' ' for bytes with no printable ASCII mapping.
[[nodiscard]] inline char ebcdicToAscii(uint8_t e) noexcept
{
    return kEbcdicToAsciiTable[e];
}

} // namespace NSNAME
