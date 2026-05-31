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

struct MeasurementSet
{
    static constexpr auto interface = "xyz.openbmc_project.Attestation.MeasurementSet";



};



} // sdbusplus::common::xyz::openbmc_project::attestation

namespace sdbusplus::message::details
{
} // namespace sdbusplus::message::details

