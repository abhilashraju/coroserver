#pragma once
/**
 * @file screen_renderer.hpp
 * @brief Renders a Screen5250 model to an ANSI terminal on stdout
 *
 * Clears the terminal, redraws all 24×80 character cells, then positions
 * the terminal cursor at the 5250 input cursor location.
 *
 * All output is assembled into a single string and written in one ::write()
 * call to avoid visible flicker.
 *
 * ANSI sequences used:
 *   \033[2J        — erase entire display
 *   \033[H         — cursor home (1,1)
 *   \033[r;cH      — move cursor to row r, col c (1-based)
 *   \033[7m        — reverse video (used for field attributes)
 *   \033[0m        — reset attributes
 */

#include "screen5250.hpp"

#include <cstdio>
#include <string>
#include <unistd.h>

namespace NSNAME
{

class ScreenRenderer
{
  public:
    /// Render the full screen to stdout.
    /// Redraws every cell on every call; suitable for a 24×80 character screen
    /// where the entire redraw is only ~1920 bytes + ANSI overhead.
    static void render(const Screen5250& screen)
    {
        std::string out;
        // Reserve: ~2000 chars + ~500 bytes of ANSI escape overhead
        out.reserve(3000);

        // Clear screen and home cursor
        out += "\033[2J\033[H";

        for (int row = 0; row < Screen5250::ROWS; ++row)
        {
            for (int col = 0; col < Screen5250::COLS; ++col)
            {
                const Cell5250& cell = screen.at(row, col);

                // Apply reverse video for non-default field attributes
                if (cell.attr != 0x20 && cell.attr != 0x00)
                    out += "\033[7m";

                char c = cell.ch;
                // Sanitise: only write printable ASCII to the terminal
                if (c < 0x20 || c > 0x7E)
                    c = ' ';
                out += c;

                if (cell.attr != 0x20 && cell.attr != 0x00)
                    out += "\033[0m";
            }
            // Move to start of next row explicitly (avoids relying on auto-wrap)
            if (row < Screen5250::ROWS - 1)
            {
                out += '\n';
            }
        }

        // Position terminal cursor at the 5250 input cursor (1-based for ANSI)
        char cursorSeq[32];
        std::snprintf(cursorSeq, sizeof(cursorSeq),
                      "\033[%d;%dH", screen.inputRow + 1, screen.inputCol + 1);
        out += cursorSeq;

        ::write(STDOUT_FILENO, out.data(), out.size());
    }

    /// Print a status line below the screen area (row 25).
    static void renderStatusLine(const std::string& msg)
    {
        char seq[128];
        // Move to row 26 (below the 24-row screen + 1 blank)
        std::snprintf(seq, sizeof(seq),
                      "\033[26;1H\033[2K%s", msg.c_str());
        ::write(STDOUT_FILENO, seq, std::strlen(seq));
    }
};

} // namespace NSNAME
