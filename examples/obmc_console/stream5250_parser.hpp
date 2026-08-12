#pragma once
/**
 * @file stream5250_parser.hpp
 * @brief IBM 5250 data stream parser
 *
 * Processes raw 5250 payload bytes (after VSLIP decode) and applies the
 * embedded orders and character data to a Screen5250 model.
 *
 * 5250 Orders handled:
 *   SBA  0x11  Set Buffer Address    — 2 address bytes follow
 *   IC   0x13  Insert Cursor         — no operands; sets input cursor
 *   SF   0x1D  Start of Field        — 1 field-attribute byte follows (skip)
 *   RA   0x3C  Repeat to Address     — 2 address bytes + 1 EBCDIC char
 *   EA   0x3F  Erase to Address      — 2 address bytes; fill with spaces
 *   MF   0x2B  Modify Field          — skip variable-length structure
 *   Clear 0x40 — handled at record level; clears the screen
 *
 * Write commands (leading byte of a complete record):
 *   WTD  0x11  Write To Display      — payload follows directly
 *   WSF  0xF3  Write Structured Field — skip (not rendered)
 *   ClearUnit 0x40                   — clear screen
 *
 * The parser is stateful; call reset() on reconnect.
 */

#include "ebcdic.hpp"
#include "screen5250.hpp"

#include <cstdint>
#include <span>

namespace NSNAME
{

class Stream5250Parser
{
  public:
    // -----------------------------------------------------------------------
    // Feed a complete decoded payload (one VSLIP frame) into the parser.
    // Modifies `screen` in place.
    // -----------------------------------------------------------------------
    void process(std::span<const uint8_t> data, Screen5250& screen)
    {
        size_t i = 0;
        const size_t n = data.size();

        // First byte of a record is a write command.
        if (n == 0)
            return;

        uint8_t cmd = data[i++];

        switch (cmd)
        {
            case 0x40: // ClearUnit — clear the screen, no further data
                screen.clear();
                return;

            case 0x11: // Write To Display — stream of orders follows
                break;

            case 0xF3: // Write Structured Field — skip entire record
                return;

            default:
                // Unknown command — attempt to parse as order stream anyway
                // (some implementations omit the command byte)
                --i;
                break;
        }

        // ----------------------------------------------------------------
        // Parse the order / character stream
        // ----------------------------------------------------------------
        while (i < n)
        {
            uint8_t b = data[i++];

            switch (b)
            {
                // SBA — Set Buffer Address (2 bytes follow)
                case 0x11:
                {
                    if (i + 1 >= n) return; // incomplete
                    auto [row, col] = Screen5250::decodeSba(data[i], data[i + 1]);
                    i += 2;
                    screen.setCursor(row, col);
                    break;
                }

                // IC — Insert Cursor (no operands)
                case 0x13:
                    screen.setInputCursor(screen.cursorRow, screen.cursorCol);
                    break;

                // SF — Start of Field (1 attribute byte follows, skip)
                case 0x1D:
                    if (i < n) ++i; // consume field attribute
                    break;

                // RA — Repeat to Address (2 addr bytes + 1 EBCDIC char)
                case 0x3C:
                {
                    if (i + 2 >= n) return; // incomplete
                    auto [toRow, toCol] = Screen5250::decodeSba(data[i], data[i + 1]);
                    i += 2;
                    char c = ebcdicToAscii(data[i++]);
                    screen.repeatToAddress(toRow, toCol, c);
                    break;
                }

                // EA — Erase to Address (2 addr bytes, fill with spaces)
                case 0x3F:
                {
                    if (i + 1 >= n) return; // incomplete
                    auto [toRow, toCol] = Screen5250::decodeSba(data[i], data[i + 1]);
                    i += 2;
                    screen.repeatToAddress(toRow, toCol, ' ');
                    break;
                }

                // MF — Modify Field: variable-length, skip to next order/char
                // (simplified: skip the attribute byte that follows)
                case 0x2B:
                    if (i < n) ++i;
                    break;

                // NUL / padding — skip
                case 0x00:
                    break;

                default:
                    // Treat as EBCDIC character data
                    screen.putChar(ebcdicToAscii(b));
                    break;
            }
        }
    }

    void reset()
    {
        // Nothing to reset currently; reserved for future stateful parsing
        // across frame boundaries.
    }
};

} // namespace NSNAME
