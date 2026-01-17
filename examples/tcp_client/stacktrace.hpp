#include <cxxabi.h>   // For demangling C++ symbols
#include <execinfo.h> // For backtrace functions

#include <iostream>
#include <string>
#include <vector>

void print_callstack_linux()
{
    std::cout << "--- Printing Call Stack (Linux/GCC) ---" << std::endl;
    const int max_frames = 64;
    void* addrlist[max_frames + 1];

    int frames = backtrace(addrlist, max_frames + 1);
    char** symbollist = backtrace_symbols(addrlist, frames);

    if (symbollist)
    {
        for (int i = 1; i < frames; ++i)
        {
            std::string symbol_line = symbollist[i];
            size_t start = symbol_line.find('(');
            size_t end = symbol_line.find('+', start);

            if (start != std::string::npos && end != std::string::npos)
            {
                std::string mangled_name =
                    symbol_line.substr(start + 1, end - (start + 1));

                int status = 0;
                size_t len = 1024;
                char* demangled_name = static_cast<char*>(malloc(len));

                demangled_name = abi::__cxa_demangle(
                    mangled_name.c_str(), demangled_name, &len, &status);

                if (status == 0)
                {
                    std::cout
                        << "  " << i << ": " << demangled_name << std::endl;
                }
                else
                {
                    std::cout << "  " << i << ": " << mangled_name << std::endl;
                }
                free(demangled_name);
            }
            else
            {
                std::cout << "  " << i << ": " << symbol_line << std::endl;
            }
        }
        free(symbollist);
    }
}
// This function can be used in the main code from the C++23 example.
