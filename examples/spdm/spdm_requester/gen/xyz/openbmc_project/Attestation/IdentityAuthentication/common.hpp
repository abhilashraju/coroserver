#pragma once
#include <algorithm>
#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>

#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/utility/dedup_variant.hpp>

namespace sdbusplus::common::xyz::openbmc_project::attestation
{

struct IdentityAuthentication
{
    static constexpr auto interface = "xyz.openbmc_project.Attestation.IdentityAuthentication";

    enum class VerificationStatus
    {
        Failed,
        Success,
        Unknown,
    };

    struct properties_t
    {
        VerificationStatus responder_verification_status = VerificationStatus::Unknown;
    };

    using PropertiesVariant = sdbusplus::utility::dedup_variant_t<
        VerificationStatus>;

    /** @brief Convert a string to an appropriate enum value.
     *  @param[in] s - The string to convert in the form of
     *                 "xyz.openbmc_project.Attestation.IdentityAuthentication.<value name>"
     *  @return - The enum value.
     *
     *  @note Throws if string is not a valid mapping.
     */
    static VerificationStatus convertVerificationStatusFromString(const std::string& s);

    /** @brief Convert a string to an appropriate enum value.
     *  @param[in] s - The string to convert in the form of
     *                 "xyz.openbmc_project.Attestation.IdentityAuthentication.<value name>"
     *  @return - The enum value or std::nullopt
     */
    static std::optional<VerificationStatus>
        convertStringToVerificationStatus(const std::string& s) noexcept;

    /** @brief Convert an enum value to a string.
     *  @param[in] e - The enum to convert to a string.
     *  @return - The string conversion in the form of
     *            "xyz.openbmc_project.Attestation.IdentityAuthentication.<value name>"
     */
    static std::string convertVerificationStatusToString(VerificationStatus e);
};

/* Specialization of sdbusplus::common::convertForMessage
 * for enum-type IdentityAuthentication::VerificationStatus.
 *
 * This converts from the enum to a constant string representing the enum.
 *
 * @param[in] e - Enum value to convert.
 * @return string representing the name for the enum value.
 */
inline std::string convertForMessage(IdentityAuthentication::VerificationStatus e)
{
    return IdentityAuthentication::convertVerificationStatusToString(e);
}


namespace details
{
using namespace std::literals::string_view_literals;

/** String to enum mapping for IdentityAuthentication::VerificationStatus */
inline constexpr std::array mappingIdentityAuthenticationVerificationStatus = {
    std::make_tuple("xyz.openbmc_project.Attestation.IdentityAuthentication.VerificationStatus.Failed"sv,
                    IdentityAuthentication::VerificationStatus::Failed ),
    std::make_tuple("xyz.openbmc_project.Attestation.IdentityAuthentication.VerificationStatus.Success"sv,
                    IdentityAuthentication::VerificationStatus::Success ),
    std::make_tuple("xyz.openbmc_project.Attestation.IdentityAuthentication.VerificationStatus.Unknown"sv,
                    IdentityAuthentication::VerificationStatus::Unknown ),
};
} //  namespace details

inline auto IdentityAuthentication::convertStringToVerificationStatus(const std::string& s) noexcept
    -> std::optional<VerificationStatus>
{
    auto i = std::find_if(std::begin(details::mappingIdentityAuthenticationVerificationStatus),
                          std::end(details::mappingIdentityAuthenticationVerificationStatus),
                          [&s](auto& e){ return s == std::get<0>(e); } );

    if (std::end(details::mappingIdentityAuthenticationVerificationStatus) == i)
    {
        return std::nullopt;
    }
    else
    {
        return std::get<1>(*i);
    }
}

inline auto IdentityAuthentication::convertVerificationStatusFromString(const std::string& s) -> VerificationStatus
{
    auto r = convertStringToVerificationStatus(s);

    if (!r)
    {
        throw sdbusplus::exception::InvalidEnumString();
    }
    else
    {
        return *r;
    }
}

inline std::string IdentityAuthentication::convertVerificationStatusToString(
    IdentityAuthentication::VerificationStatus v)
{
    auto i = std::find_if(std::begin(details::mappingIdentityAuthenticationVerificationStatus),
                          std::end(details::mappingIdentityAuthenticationVerificationStatus),
                          [v](auto& e){ return v == std::get<1>(e); });

    if (i == std::end(details::mappingIdentityAuthenticationVerificationStatus))
    {
        throw std::invalid_argument(std::to_string(static_cast<int>(v)));
    }
    return std::string(std::get<0>(*i));
}

} // sdbusplus::common::xyz::openbmc_project::attestation

namespace sdbusplus::message::details
{
template <>
struct convert_from_string<common::xyz::openbmc_project::attestation::IdentityAuthentication::VerificationStatus>
{
    static auto op(const std::string& value) noexcept
    {
        return common::xyz::openbmc_project::attestation::IdentityAuthentication::
            convertStringToVerificationStatus(value);
    }
};

template <>
struct convert_to_string<common::xyz::openbmc_project::attestation::IdentityAuthentication::VerificationStatus>
{
    static std::string
        op(common::xyz::openbmc_project::attestation::IdentityAuthentication::VerificationStatus value)
    {
        return common::xyz::openbmc_project::attestation::IdentityAuthentication::
            convertVerificationStatusToString(value);
    }
};
} // namespace sdbusplus::message::details

