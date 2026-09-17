#pragma once

#include "ebcdic.hpp"
#include "vslip_framer.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace NSNAME
{

class IbmiEmulator
{
  public:
    [[nodiscard]] std::vector<uint8_t> initialDisplay()
    {
        return makeDisplay();
    }

    [[nodiscard]] std::vector<uint8_t> process(std::span<const uint8_t> input)
    {
        std::vector<std::vector<uint8_t>> frames;
        framer_.feed(input, frames);
        for (const std::vector<uint8_t>& frame : frames)
        {
            if (!frame.empty())
            {
                processFrame(frame);
            }
        }
        return makeDisplay();
    }

    [[nodiscard]] bool isClosed() const
    {
        return closed_;
    }

  private:
    enum class Screen
    {
        signOn,
        menu
    };

    enum class Field
    {
        user,
        password,
        selection
    };

    static constexpr std::string_view validUser = "DEMO";
    static constexpr std::string_view validPassword = "PASSWORD";
    static constexpr size_t maxInputLength = 20;

    static uint8_t asciiToEbcdic(char ascii)
    {
        if (ascii == ' ')
        {
            return 0x40;
        }
        for (size_t i = 0; i < 256; ++i)
        {
            if (kEbcdicToAsciiTable[i] == ascii)
            {
                return static_cast<uint8_t>(i);
            }
        }
        return 0x40;
    }

    static void writeText(std::vector<uint8_t>& display, uint8_t row,
                          uint8_t column, std::string_view text)
    {
        display.insert(display.end(), {0x11, row, column});
        for (char character : text)
        {
            display.push_back(asciiToEbcdic(character));
        }
    }

    static void setInputCursor(std::vector<uint8_t>& display, uint8_t row,
                               uint8_t column)
    {
        display.insert(display.end(), {0x11, row, column, 0x1D, 0x20, 0x13});
    }

    [[nodiscard]] std::vector<uint8_t> makeDisplay() const
    {
        if (closed_)
        {
            return VSlipFramer::encode(std::vector<uint8_t>{0x40});
        }

        std::vector<uint8_t> display{0x11};
        if (screen_ == Screen::signOn)
        {
            writeText(
                display, 1, 0,
                "IBM i DUMMY CONSOLE                         SYSTEM: TESTIBMI");
            writeText(display, 2, 0,
                      "5250 emulator source for ibmi_console_client testing");
            writeText(display, 4, 0, "                         Sign On");
            writeText(display, 7, 8,
                      "User . . . . . . . . . . . . . . . . . . . .:");
            writeText(display, 7, 56, user_);
            writeText(display, 9, 8,
                      "Password . . . . . . . . . . . . . . . . . .:");
            writeText(display, 9, 56, std::string(password_.size(), '*'));
            writeText(display, 13, 0, status_);
            writeText(display, 20, 0,
                      "Enter=Next/Sign on   Up/Down=Change field");
            writeText(display, 22, 0, "F3=Exit   F12=Clear fields");
            setInputCursor(display, field_ == Field::user ? 7 : 9,
                           static_cast<uint8_t>(56 + inputCursor_));
        }
        else
        {
            writeText(
                display, 1, 0,
                "IBM i DUMMY CONSOLE                         SYSTEM: TESTIBMI");
            writeText(display, 4, 0, "                        Main Menu");
            writeText(display, 7, 8, "1. Work with user profiles");
            writeText(display, 9, 8, "2. Work with jobs");
            writeText(display, 11, 8, "3. Display system status");
            writeText(display, 14, 8,
                      "Selection . . . . . . . . . . . . . . . . . .:");
            writeText(display, 14, 56, selection_);
            writeText(display, 17, 0, status_);
            writeText(display, 20, 0, "Type 1, 2, or 3 and press Enter.");
            writeText(display, 22, 0, "F3=Exit   F12=Sign off");
            setInputCursor(display, 14,
                           static_cast<uint8_t>(56 + inputCursor_));
        }
        std::vector<uint8_t> output =
            VSlipFramer::encode(std::vector<uint8_t>{0x40});
        const std::vector<uint8_t> displayFrame = VSlipFramer::encode(display);
        output.insert(output.end(), displayFrame.begin(), displayFrame.end());
        return output;
    }

    void processFrame(const std::vector<uint8_t>& frame)
    {
        std::string* input = screen_ == Screen::signOn
                                 ? (field_ == Field::user ? &user_ : &password_)
                                 : &selection_;
        switch (frame.front())
        {
            case 0xF1:
                if (screen_ == Screen::signOn && field_ == Field::user)
                {
                    field_ = Field::password;
                    inputCursor_ = password_.size();
                    status_ = "Enter password, then press Enter.";
                }
                else if (screen_ == Screen::signOn)
                {
                    if (user_ == validUser && password_ == validPassword)
                    {
                        screen_ = Screen::menu;
                        field_ = Field::selection;
                        inputCursor_ = 0;
                        status_ = "Sign on successful. Select an option.";
                    }
                    else
                    {
                        password_.clear();
                        inputCursor_ = 0;
                        status_ = "Sign on failed. Use DEMO / PASSWORD.";
                    }
                }
                else if (selection_ == "1" || selection_ == "2" ||
                         selection_ == "3")
                {
                    status_ = "Option " + selection_ + " selected.";
                    selection_.clear();
                    inputCursor_ = 0;
                }
                else
                {
                    status_ = "Enter 1, 2, or 3.";
                }
                break;
            case 0x33:
                closed_ = true;
                return;
            case 0x3C:
                if (screen_ == Screen::menu)
                {
                    screen_ = Screen::signOn;
                    field_ = Field::user;
                    user_.clear();
                    password_.clear();
                    selection_.clear();
                    status_ = "Signed off. Enter DEMO and PASSWORD.";
                }
                else
                {
                    user_.clear();
                    password_.clear();
                    field_ = Field::user;
                    status_ = "Fields cleared.";
                }
                inputCursor_ = 0;
                break;
            case 0x15:
                if (inputCursor_ > 0)
                {
                    input->erase(--inputCursor_, 1);
                }
                break;
            case 0x1A:
                if (inputCursor_ < input->size())
                {
                    ++inputCursor_;
                }
                break;
            case 0x1B:
                if (inputCursor_ > 0)
                {
                    --inputCursor_;
                }
                break;
            case 0x18:
            case 0x19:
                if (screen_ == Screen::signOn)
                {
                    field_ =
                        field_ == Field::user ? Field::password : Field::user;
                    inputCursor_ =
                        field_ == Field::user ? user_.size() : password_.size();
                }
                break;
            default:
                for (uint8_t byte : frame)
                {
                    const char character = ebcdicToAscii(byte);
                    if (character != ' ' && input->size() < maxInputLength)
                    {
                        input->insert(inputCursor_++, 1, character);
                    }
                }
                break;
        }
    }

    VSlipFramer framer_;
    Screen screen_ = Screen::signOn;
    Field field_ = Field::user;
    std::string user_;
    std::string password_;
    std::string selection_;
    std::string status_ = "Enter DEMO and PASSWORD to sign on.";
    size_t inputCursor_ = 0;
    bool closed_ = false;
};

} // namespace NSNAME
