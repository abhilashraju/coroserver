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

struct ComponentIntegrity
{
    static constexpr auto interface = "xyz.openbmc_project.Attestation.ComponentIntegrity";

    enum class SecurityTechnologyType
    {
        OEM,
        SPDM,
        TPM,
        Unknown,
    };

    struct properties_t
    {
        bool enabled = false;
        SecurityTechnologyType type = SecurityTechnologyType::Unknown;
        std::string type_version;
        uint64_t last_updated;
    };

    using PropertiesVariant = sdbusplus::utility::dedup_variant_t<
        SecurityTechnologyType,
        bool,
        std::string,
        uint64_t>;

    /** @brief Convert a string to an appropriate enum value.
     *  @param[in] s - The string to convert in the form of
     *                 "xyz.openbmc_project.Attestation.ComponentIntegrity.<value name>"
     *  @return - The enum value.
     *
     *  @note Throws if string is not a valid mapping.
     */
    static SecurityTechnologyType convertSecurityTechnologyTypeFromString(const std::string& s);

    /** @brief Convert a string to an appropriate enum value.
     *  @param[in] s - The string to convert in the form of
     *                 "xyz.openbmc_project.Attestation.ComponentIntegrity.<value name>"
     *  @return - The enum value or std::nullopt
     */
    static std::optional<SecurityTechnologyType>
        convertStringToSecurityTechnologyType(const std::string& s) noexcept;

    /** @brief Convert an enum value to a string.
     *  @param[in] e - The enum to convert to a string.
     *  @return - The string conversion in the form of
     *            "xyz.openbmc_project.Attestation.ComponentIntegrity.<value name>"
     */
    static std::string convertSecurityTechnologyTypeToString(SecurityTechnologyType e);
};

/* Specialization of sdbusplus::common::convertForMessage
 * for enum-type ComponentIntegrity::SecurityTechnologyType.
 *
 * This converts from the enum to a constant string representing the enum.
 *
 * @param[in] e - Enum value to convert.
 * @return string representing the name for the enum value.
 */
inline std::string convertForMessage(ComponentIntegrity::SecurityTechnologyType e)
{
    return ComponentIntegrity::convertSecurityTechnologyTypeToString(e);
}


namespace details
{
using namespace std::literals::string_view_literals;

/** String to enum mapping for ComponentIntegrity::SecurityTechnologyType */
inline constexpr std::array mappingComponentIntegritySecurityTechnologyType = {
    std::make_tuple("xyz.openbmc_project.Attestation.ComponentIntegrity.SecurityTechnologyType.OEM"sv,
                    ComponentIntegrity::SecurityTechnologyType::OEM ),
    std::make_tuple("xyz.openbmc_project.Attestation.ComponentIntegrity.SecurityTechnologyType.SPDM"sv,
                    ComponentIntegrity::SecurityTechnologyType::SPDM ),
    std::make_tuple("xyz.openbmc_project.Attestation.ComponentIntegrity.SecurityTechnologyType.TPM"sv,
                    ComponentIntegrity::SecurityTechnologyType::TPM ),
    std::make_tuple("xyz.openbmc_project.Attestation.ComponentIntegrity.SecurityTechnologyType.Unknown"sv,
                    ComponentIntegrity::SecurityTechnologyType::Unknown ),
};
} //  namespace details

inline auto ComponentIntegrity::convertStringToSecurityTechnologyType(const std::string& s) noexcept
    -> std::optional<SecurityTechnologyType>
{
    auto i = std::find_if(std::begin(details::mappingComponentIntegritySecurityTechnologyType),
                          std::end(details::mappingComponentIntegritySecurityTechnologyType),
                          [&s](auto& e){ return s == std::get<0>(e); } );

    if (std::end(details::mappingComponentIntegritySecurityTechnologyType) == i)
    {
        return std::nullopt;
    }
    else
    {
        return std::get<1>(*i);
    }
}

inline auto ComponentIntegrity::convertSecurityTechnologyTypeFromString(const std::string& s) -> SecurityTechnologyType
{
    auto r = convertStringToSecurityTechnologyType(s);

    if (!r)
    {
        throw sdbusplus::exception::InvalidEnumString();
    }
    else
    {
        return *r;
    }
}

inline std::string ComponentIntegrity::convertSecurityTechnologyTypeToString(
    ComponentIntegrity::SecurityTechnologyType v)
{
    auto i = std::find_if(std::begin(details::mappingComponentIntegritySecurityTechnologyType),
                          std::end(details::mappingComponentIntegritySecurityTechnologyType),
                          [v](auto& e){ return v == std::get<1>(e); });

    if (i == std::end(details::mappingComponentIntegritySecurityTechnologyType))
    {
        throw std::invalid_argument(std::to_string(static_cast<int>(v)));
    }
    return std::string(std::get<0>(*i));
}

} // sdbusplus::common::xyz::openbmc_project::attestation

namespace sdbusplus::message::details
{
template <>
struct convert_from_string<common::xyz::openbmc_project::attestation::ComponentIntegrity::SecurityTechnologyType>
{
    static auto op(const std::string& value) noexcept
    {
        return common::xyz::openbmc_project::attestation::ComponentIntegrity::
            convertStringToSecurityTechnologyType(value);
    }
};

template <>
struct convert_to_string<common::xyz::openbmc_project::attestation::ComponentIntegrity::SecurityTechnologyType>
{
    static std::string
        op(common::xyz::openbmc_project::attestation::ComponentIntegrity::SecurityTechnologyType value)
    {
        return common::xyz::openbmc_project::attestation::ComponentIntegrity::
            convertSecurityTechnologyTypeToString(value);
    }
};
} // namespace sdbusplus::message::details

