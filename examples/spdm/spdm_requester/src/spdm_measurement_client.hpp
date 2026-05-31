#pragma once
#include "logger.hpp"
#include "spdmglobal.hpp"

extern "C"
{
#include <internal/libspdm_common_lib.h>
#include <library/spdm_requester_lib.h>
}

#include <optional>
#include <string>
#include <tuple>
#include <vector>

// Handles SPDM measurement retrieval operations
class SpdmMeasurementClient
{
  public:
    explicit SpdmMeasurementClient(void* spdmContext) :
        spdmContext_(spdmContext)
    {}

    std::optional<std::string> getSignedMeasurementsImpl(
        const std::vector<size_t>& measurementIndices, const std::string& nonce,
        size_t slotId)
    {
        // If no specific indices requested, get all measurements
        std::vector<size_t> indicesToProcess = measurementIndices;
        if (indicesToProcess.empty())
        {
            LOG_INFO(
                "No specific measurement indices provided, requesting all measurements.\n");
            indicesToProcess = {255}; // Request all measurements
        }

        // Buffer to collect all measurements
        std::vector<uint8_t> allMeasurements;

        // Loop through each measurement index and get measurements individually
        for (const auto& idx : indicesToProcess)
        {
            auto [success, data] = getSingleMeasurement(idx, slotId);
            if (!success)
            {
                LOG_ERROR("failed to get measurements");
                return std::nullopt;
            }
            auto [measurementData, numberOfBlocks] = data;
            processMeasurementData(idx, measurementData, allMeasurements,
                                   numberOfBlocks);
        }

        // Convert combined measurements to base64 for D-Bus transport
        std::string base64Measurement = encodeBase64(allMeasurements);

        LOG_INFO("All measurement are fetched");

        return std::optional<std::string>{base64Measurement};
    }

    std::pair<bool, std::pair<std::vector<uint8_t>, uint8_t>>
        getSingleMeasurement(size_t measurementIndex, size_t slotId)
    {
        uint8_t measurementOperation = static_cast<uint8_t>(measurementIndex);

        // Buffer for single measurement response
        constexpr size_t MAX_MEASUREMENT_SIZE = 4096;
        std::vector<uint8_t> measurementBuffer(MAX_MEASUREMENT_SIZE);
        uint32_t measurementSize = measurementBuffer.size();
        uint8_t contentChanged = 0;
        uint8_t numberOfBlocks = 0;

        // Call libspdm to get single measurement
        libspdm_return_t status = libspdm_get_measurement(
            spdmContext_,
            nullptr, // No session
            0,       // request_attribute
            measurementOperation, static_cast<uint8_t>(slotId), &contentChanged,
            &numberOfBlocks, &measurementSize, measurementBuffer.data());

        if (LIBSPDM_STATUS_IS_ERROR(status))
        {
            LOG_ERROR("libspdm_get_measurement failed, status: {}", status);
            return std::make_pair(false,
                                  std::make_pair(std::vector<uint8_t>{}, 0));
        }

        // Resize buffer to actual measurement size
        measurementBuffer.resize(measurementSize);

        return std::make_pair(
            true, std::make_pair(measurementBuffer, numberOfBlocks));
    }

    void processMeasurementData(
        size_t measurementIndex, const std::vector<uint8_t>& measurementData,
        std::vector<uint8_t>& allMeasurements, uint8_t numberOfBlocks)
    {
        // For index 0 (total count), we only get the count, not actual
        // measurements
        if (measurementIndex == 0)
        {
            LOG_INFO(
                "Measurement index 0 indicates total count: {} measurements available.",
                measurementData[0]);
        }
        else
        {
            // Append this measurement to the combined buffer
            allMeasurements.insert(allMeasurements.end(),
                                   measurementData.begin(),
                                   measurementData.end());

            LOG_DEBUG(
                "Appended measurement index {}, size {} bytes, total combined size now {} bytes.",
                measurementIndex, measurementData.size(),
                allMeasurements.size());
        }
    }

  private:
    // Base64 encoding implementation
    std::string encodeBase64(const std::vector<uint8_t>& data)
    {
        static const char b64_table[] =
            "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        std::string result;
        size_t i = 0;
        for (; i + 2 < data.size(); i += 3)
        {
            uint32_t n = (data[i] << 16) | (data[i + 1] << 8) | data[i + 2];
            result += b64_table[(n >> 18) & 63];
            result += b64_table[(n >> 12) & 63];
            result += b64_table[(n >> 6) & 63];
            result += b64_table[n & 63];
        }
        if (i < data.size())
        {
            uint32_t n = data[i] << 16;
            result += b64_table[(n >> 18) & 63];
            if (i + 1 < data.size())
            {
                n |= data[i + 1] << 8;
                result += b64_table[(n >> 12) & 63];
                result += b64_table[(n >> 6) & 63];
                result += '=';
            }
            else
            {
                result += b64_table[(n >> 12) & 63];
                result += "==";
            }
        }
        return result;
    }

    void* spdmContext_;
};
