#pragma once
#include <pwd.h>
#include <security/pam_appl.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#include <boost/process.hpp>

#include <filesystem>
#include <format>
#include <fstream>
#include <iostream>
namespace NSNAME
{
namespace lg2
{
template <typename... Args>
void error(Args&&... args)
{
    (std::cerr << ... << args) << std::endl;
};
} // namespace lg2
template <typename T>
void elog()
{
    throw std::runtime_error("Error");
}
struct InternalFailure
{};
const char* authAppPath = "/usr/bin/google-authenticator";
const char* secretKeyPath = "/home/{}/.google_authenticator";
constexpr std::string_view secretKeyTempPath =
    "/home/{}/.google_authenticator.tmp";
bool checkMfaStatus()
{
    return true;
}
template <typename... ArgTypes>
std::vector<std::string> executeCmd(const char* path, ArgTypes&&... tArgs)
{
    std::vector<std::string> stdOutput;
    boost::process::ipstream stdOutStream;
    boost::process::child execProg(path, const_cast<char*>(tArgs)...,
                                   boost::process::std_out > stdOutStream);
    std::string stdOutLine;

    while (stdOutStream && std::getline(stdOutStream, stdOutLine) &&
           !stdOutLine.empty())
    {
        stdOutput.emplace_back(stdOutLine);
    }

    execProg.wait();

    int retCode = execProg.exit_code();
    if (retCode)
    {
        lg2::error("Command {PATH} execution failed, return code {RETCODE}",
                   "PATH", path, "RETCODE", retCode);
        elog<InternalFailure>();
    }

    return stdOutput;
}
bool changeFileOwnership(const std::string& filePath,
                         const std::string& userName)
{
    // Get the user ID
    struct passwd* pwd = getpwnam(userName.c_str());
    if (pwd == nullptr)
    {
        lg2::error("Failed to get user ID for user:{USER}", "USER", userName);
        return false;
    }
    // Change the ownership of the file
    if (chown(filePath.c_str(), pwd->pw_uid, pwd->pw_gid) != 0)
    {
        lg2::error("Ownership change error {PATH}", "PATH", filePath);
        return false;
    }
    return true;
}
std::string createSecretKey(std::string userName)
{
    if (!checkMfaStatus())
    {
        elog<InternalFailure>();
        return "";
    }
    if (!std::filesystem::exists(authAppPath))
    {
        lg2::error("No authenticator app found at {PATH}", "PATH", authAppPath);
        elog<InternalFailure>();
        return "";
    }
    std::string path = std::format(secretKeyTempPath, userName);
    /*
    -u no-rate-limit
    -W minimal-window
    -Q qr-mode (NONE, ANSI, UTF8)
    -t time-based
    -f force file
    -D allow-reuse
    -C no-confirm no confirmation required for code provisioned
    */
    executeCmd(authAppPath, "-s", path.c_str(), "-u", "-W", "-Q", "NONE", "-t",
               "-f", "-D", "-C");
    if (!std::filesystem::exists(path))
    {
        lg2::error("Failed to create secret key for user {USER}", "USER",
                   userName);
        elog<InternalFailure>();
        return "";
    }
    std::ifstream file(path);
    if (!file.is_open())
    {
        lg2::error("Failed to open secret key file {PATH}", "PATH", path);
        elog<InternalFailure>();
        return "";
    }
    std::string secret;
    std::getline(file, secret);
    file.close();
    if (!changeFileOwnership(path, userName))
    {
        elog<InternalFailure>();
        return "";
    }
    return secret;
}
}