#pragma once

#include "logger.hpp"

#include <cstdint>
#include <utility>
#include <vector>

extern "C"
{
#include <library/spdm_common_lib.h>
#include <hal/library/responder/measlib.h>
}

namespace spdm_utils
{

/**
 * @brief Helper function to get a single measurement from SPDM responder
 * @param spdmContext The SPDM context (can be nullptr for testing)
 * @param measurementIndex The index of the measurement to retrieve
 * @return Pair of (success, (measurement_data, number_of_blocks))
 *
 * This is a utility for testing/debugging. In normal operation,
 * measurements are retrieved by the requester through SPDM protocol,
 * and the responder automatically handles them via
 * libspdm_measurement_collection
 */
inline std::pair<bool, std::pair<std::vector<uint8_t>, uint8_t>>
    getSingleMeasurement(void* spdmContext, size_t measurementIndex)
{
    uint8_t measurementOperation = static_cast<uint8_t>(measurementIndex);
    constexpr size_t MAX_MEASUREMENT_SIZE = 4096;
    std::vector<uint8_t> measurementBuffer(MAX_MEASUREMENT_SIZE);
    size_t measurementSize = measurementBuffer.size();
    uint8_t contentChanged = 0;
    uint8_t numberOfBlocks = 0;

    // Call the measurement collection callback directly
    libspdm_return_t status = libspdm_measurement_collection(
        spdmContext,
        nullptr,                 // session_id
        SPDM_MESSAGE_VERSION_11, // SPDM version 1.1
        SPDM_MEASUREMENT_SPECIFICATION_DMTF,
        SPDM_ALGORITHMS_MEASUREMENT_HASH_ALGO_TPM_ALG_SHA_384,
        measurementOperation,
        0,       // request_attribute
        nullptr, // requester_nonce
        0,       // slot_id_param
        0,       // request_context_size
        nullptr, // request_context
        &contentChanged, &numberOfBlocks, measurementBuffer.data(),
        &measurementSize);

    if (LIBSPDM_STATUS_IS_ERROR(status))
    {
        LOG_ERROR("libspdm_measurement_collection failed, status: {}", status);
        return std::make_pair(false, std::make_pair(std::vector<uint8_t>{}, 0));
    }

    // Resize buffer to actual measurement size
    measurementBuffer.resize(measurementSize);

    LOG_DEBUG("Retrieved measurement index {}, size {} bytes, {} blocks",
              measurementIndex, measurementSize, numberOfBlocks);

    return std::make_pair(true,
                          std::make_pair(measurementBuffer, numberOfBlocks));
}

} // namespace spdm_utils
