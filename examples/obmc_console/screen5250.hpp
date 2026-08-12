#pragma once
/**
 * @file screen5250.hpp
 * @brief IBM 5250 24×80 character-cell screen model
 *
 * Maintains the current state of the IBMi display as a grid of character
 * cells.  The 5250 stream parser writes to this model; the renderer reads it.
 *
 * Screen layout: 24 rows × 80 columns (row 0 = top, col 0 = left).
 * Cursor wraps: advancing past col 79 moves to col 0 of the next row;
 * advancing past row 23 wraps back to row 0.
 *
 * SBA (Set Buffer Address) packing:
 *   The two address bytes from a 5250 SBA order each carry a 6-bit value:
 *     row = byte0 & 0x3F   (0-based)
 *     col = byte1 & 0x3F   (0-based)
 */

#include <algorithm>
#include <array>
#include <cstdint>

namespace NSNAME
{

/// A single character cell on the 5250 screen.
struct Cell5250
{
    char    ch   = ' ';   ///< ASCII character (after EBCDIC conversion)
    uint8_t attr = 0x20;  ///< Display attribute byte (colour / highlight)
};

class Screen5250
{
  public:
    static constexpr int ROWS = 24;
    static constexpr int COLS = 80;

    // -----------------------------------------------------------------------
    // Screen state
    // -----------------------------------------------------------------------
    std::array<Cell5250, ROWS * COLS> cells{};
    int cursorRow = 0;  ///< Current write position (0-based)
    int cursorCol = 0;
    int inputRow  = 0;  ///< IC order sets the input cursor position
    int inputCol  = 0;

    // -----------------------------------------------------------------------
    // Operations
    // -----------------------------------------------------------------------

    /// Blank the entire screen and home the cursor.
    void clear()
    {
        cells.fill(Cell5250{});
        cursorRow = cursorCol = 0;
        inputRow  = inputCol  = 0;
    }

    /// Move the write cursor (SBA, WTD positioning).
    void setCursor(int row, int col)
    {
        cursorRow = std::clamp(row, 0, ROWS - 1);
        cursorCol = std::clamp(col, 0, COLS - 1);
    }

    /// Set the input cursor (IC order) — used by the renderer to position
    /// the terminal cursor after drawing.
    void setInputCursor(int row, int col)
    {
        inputRow = std::clamp(row, 0, ROWS - 1);
        inputCol = std::clamp(col, 0, COLS - 1);
    }

    /// Write an ASCII character at the current cursor position and advance.
    void putChar(char c, uint8_t attr = 0x20)
    {
        at(cursorRow, cursorCol) = Cell5250{c, attr};
        advance();
    }

    /// Fill from the current cursor to (toRow, toCol) with character c.
    /// Used by the RA (Repeat to Address) order.
    void repeatToAddress(int toRow, int toCol, char c, uint8_t attr = 0x20)
    {
        int target = toRow * COLS + toCol;
        while (true)
        {
            int pos = cursorRow * COLS + cursorCol;
            if (pos == target)
                break;
            at(cursorRow, cursorCol) = Cell5250{c, attr};
            advance();
        }
    }

    // -----------------------------------------------------------------------
    // Accessors
    // -----------------------------------------------------------------------
    [[nodiscard]] Cell5250& at(int row, int col)
    {
        return cells[static_cast<size_t>(row * COLS + col)];
    }

    [[nodiscard]] const Cell5250& at(int row, int col) const
    {
        return cells[static_cast<size_t>(row * COLS + col)];
    }

    // -----------------------------------------------------------------------
    // Decode a packed 5250 SBA address pair into (row, col).
    // Each byte contributes 6 bits.
    // -----------------------------------------------------------------------
    static std::pair<int, int> decodeSba(uint8_t b0, uint8_t b1)
    {
        return {b0 & 0x3F, b1 & 0x3F};
    }

  private:
    /// Advance cursor by one cell, wrapping at row/col boundaries.
    void advance()
    {
        if (++cursorCol >= COLS)
        {
            cursorCol = 0;
            if (++cursorRow >= ROWS)
                cursorRow = 0;
        }
    }
};

} // namespace NSNAME
