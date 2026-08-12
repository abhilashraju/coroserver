#pragma once
/**
 * @file vslip_framer.hpp
 * @brief VSLIP (Virtual SLIP) packet framing — RFC 1055 byte stuffing
 *
 * Encodes raw payloads into VSLIP frames and decodes incoming byte streams
 * back to payloads.  The framer is stateful so it handles incomplete frames
 * that span multiple read() calls.
 *
 * Constants:
 *   END     = 0xC0  — packet boundary marker
 *   ESC     = 0xDB  — escape byte
 *   ESC_END = 0xDC  — escaped END  (0xDB 0xDC → 0xC0 in payload)
 *   ESC_ESC = 0xDD  — escaped ESC  (0xDB 0xDD → 0xDB in payload)
 */

#include <cstdint>
#include <span>
#include <vector>

namespace NSNAME
{

class VSlipFramer
{
  public:
    // -----------------------------------------------------------------------
    // Constants
    // -----------------------------------------------------------------------
    static constexpr uint8_t END     = 0xC0;
    static constexpr uint8_t ESC     = 0xDB;
    static constexpr uint8_t ESC_END = 0xDC;
    static constexpr uint8_t ESC_ESC = 0xDD;

    // -----------------------------------------------------------------------
    // Stateless encode
    // Wraps a payload in VSLIP framing: END <escaped-payload> END
    // -----------------------------------------------------------------------
    static std::vector<uint8_t> encode(std::span<const uint8_t> payload)
    {
        std::vector<uint8_t> frame;
        frame.reserve(payload.size() + 2);
        frame.push_back(END);
        for (uint8_t b : payload)
        {
            if (b == END)
            {
                frame.push_back(ESC);
                frame.push_back(ESC_END);
            }
            else if (b == ESC)
            {
                frame.push_back(ESC);
                frame.push_back(ESC_ESC);
            }
            else
            {
                frame.push_back(b);
            }
        }
        frame.push_back(END);
        return frame;
    }

    // -----------------------------------------------------------------------
    // Stateful streaming decode
    //
    // Feed raw bytes from the socket into feed().  Each time a complete frame
    // is finished, it is appended to `out` and the internal buffer is reset.
    // Returns the number of complete frames appended.
    // -----------------------------------------------------------------------
    int feed(std::span<const uint8_t> raw, std::vector<std::vector<uint8_t>>& out)
    {
        int completed = 0;
        for (uint8_t b : raw)
        {
            if (escaping_)
            {
                escaping_ = false;
                if (b == ESC_END)
                    buf_.push_back(END);
                else if (b == ESC_ESC)
                    buf_.push_back(ESC);
                // else: malformed — drop the byte
                continue;
            }

            if (b == END)
            {
                if (!buf_.empty())
                {
                    out.push_back(std::move(buf_));
                    buf_.clear();
                    ++completed;
                }
                // else: leading END or back-to-back END — skip silently
                continue;
            }

            if (b == ESC)
            {
                escaping_ = true;
                continue;
            }

            buf_.push_back(b);
        }
        return completed;
    }

    // Reset internal state (call on reconnect)
    void reset()
    {
        buf_.clear();
        escaping_ = false;
    }

  private:
    std::vector<uint8_t> buf_;
    bool escaping_ = false;
};

} // namespace NSNAME
