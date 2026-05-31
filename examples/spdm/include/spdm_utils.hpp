#pragma once
#include "logger.hpp"
#include "spdmglobal.hpp"
extern "C"
{
#include "spdm_device_secret_lib_internal.h"

#include <library/spdm_requester_lib.h>
}
// Include SPDM I/O redirection for capturing libspdm stdout/stderr
#include "spdm_io_redirect.hpp"

template <typename Type>
void getLocalData(void* context, libspdm_data_type_t data_type, Type& outData)
{
    libspdm_data_parameter_t parameter = {};
    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    size_t data_size = sizeof(Type);
    libspdm_get_data(context, data_type, &parameter, &outData, &data_size);
}
template <typename Type>
void getRemoteData(void* context, libspdm_data_type_t data_type, Type& outData)
{
    libspdm_data_parameter_t parameter = {};
    parameter.location = LIBSPDM_DATA_LOCATION_CONNECTION;
    size_t data_size = sizeof(Type);
    libspdm_get_data(context, data_type, &parameter, &outData, &data_size);
}
template <typename Type>
void setLocalData(void* context, libspdm_data_type_t data_type,
                  const Type& data)
{
    libspdm_data_parameter_t parameter = {};
    parameter.location = LIBSPDM_DATA_LOCATION_LOCAL;
    size_t data_size = sizeof(Type);
    libspdm_set_data(context, data_type, &parameter, &data, data_size);
}
template <typename Type>
void setRemoteData(void* context, libspdm_data_type_t data_type,
                   const Type& data)
{
    libspdm_data_parameter_t parameter = {};
    parameter.location = LIBSPDM_DATA_LOCATION_CONNECTION;
    size_t data_size = sizeof(Type);
    libspdm_set_data(context, data_type, &parameter, &data, data_size);
}
extern "C"
{
#define LIBSPDM_MAX_DEBUG_MESSAGE_LENGTH 0x10000
#ifndef LIBSPDM_DEBUG_LEVEL_CONFIG
#define LIBSPDM_DEBUG_LEVEL_CONFIG (LIBSPDM_DEBUG_INFO | LIBSPDM_DEBUG_ERROR)
#endif

/**
 * @brief Custom SPDM logger that integrates with the existing logger framework
 *
 * This function is called by libspdm library for all debug output.
 * It routes SPDM debug messages to the appropriate log level based on
 * the error_level parameter from libspdm.
 *
 * @param error_level SPDM debug level (LIBSPDM_DEBUG_INFO, LIBSPDM_DEBUG_ERROR,
 * etc.)
 * @param format Printf-style format string
 * @param ... Variable arguments for format string
 */
inline void libspdm_debug_print(size_t error_level, const char* format, ...)
{
    char buffer[LIBSPDM_MAX_DEBUG_MESSAGE_LENGTH];
    va_list marker;

    // Check if this log level is enabled
    if ((error_level & LIBSPDM_DEBUG_LEVEL_CONFIG) == 0)
    {
        return;
    }

    va_start(marker, format);
    vsnprintf(buffer, sizeof(buffer), format, marker);
    va_end(marker);

    // Route to appropriate log level based on SPDM error level
    // LIBSPDM_DEBUG_ERROR = 0x80000000
    // LIBSPDM_DEBUG_WARN  = 0x00000002
    // LIBSPDM_DEBUG_INFO  = 0x00000040
    // LIBSPDM_DEBUG_VERBOSE = 0x00400000

    if (error_level & 0x80000000) // LIBSPDM_DEBUG_ERROR
    {
        LOG_ERROR("[SPDM] {}", buffer);
    }
    else if (error_level & 0x00000002) // LIBSPDM_DEBUG_WARN
    {
        LOG_WARNING("[SPDM] {}", buffer);
    }
    else if (error_level & 0x00000040) // LIBSPDM_DEBUG_INFO
    {
        LOG_INFO("[SPDM] {}", buffer);
    }
    else // LIBSPDM_DEBUG_VERBOSE or other levels
    {
        LOG_DEBUG("[SPDM] {}", buffer);
    }
}
}
