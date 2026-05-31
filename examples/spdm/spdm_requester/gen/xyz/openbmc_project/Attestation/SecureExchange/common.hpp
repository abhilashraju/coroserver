#pragma once
#include <sdbusplus/exception.hpp>
#include <sdbusplus/message.hpp>
#include <sdbusplus/utility/dedup_variant.hpp>

#include <string>
#include <tuple>

namespace sdbusplus::common::xyz::openbmc_project::attestation
{

struct SecureExchange
{
    static constexpr auto interface =
        "xyz.openbmc_project.Attestation.SecureExchange";
};

} // namespace sdbusplus::common::xyz::openbmc_project::attestation
