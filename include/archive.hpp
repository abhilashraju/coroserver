#pragma once
#include "logger.hpp"

#include <archive.h>
#include <archive_entry.h>

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

inline bool createTarArchive(const std::string& directoryPath,
                             const std::string& tarFilePath)
{
    char buff[8192] = {0};
    int len{0};
    archive* a = archive_write_new();
    archive_write_set_format_pax_restricted(a);
    archive_write_open_filename(a, tarFilePath.c_str());

    for (const auto& entry : fs::recursive_directory_iterator(directoryPath))
    {
        if (!entry.is_directory())
        {
            std::ifstream file(entry.path().string(), std::ios::binary);
            if (!file.is_open())
            {
                LOG_ERROR("Failed to open file: {}", entry.path().string());
                archive_write_close(a);
                archive_write_free(a);
                return false;
            }

            std::string relativePath =
                fs::relative(entry.path(), directoryPath).string();
            struct archive_entry* aentry = archive_entry_new();
            archive_entry_set_pathname(aentry, relativePath.c_str());
            archive_entry_set_size(aentry, fs::file_size(entry.path()));
            archive_entry_set_filetype(aentry, AE_IFREG);
            archive_entry_set_perm(aentry, 0644);
            archive_write_header(a, aentry);

            while (file.read(buff, sizeof(buff)))
            {
                len = file.gcount();
                archive_write_data(a, buff, len);
            }
            len = file.gcount();
            if (len > 0)
            {
                archive_write_data(a, buff, len);
            }
            archive_entry_free(aentry);
        }
    }

    archive_write_close(a);
    archive_write_free(a);

    return true;
}

inline int copy_data(struct archive* ar, struct archive* aw)
{
    const void* buff;
    size_t size;
    la_int64_t offset;

    while (true)
    {
        int r = archive_read_data_block(ar, &buff, &size, &offset);
        if (r == ARCHIVE_EOF)
            return ARCHIVE_OK;
        if (r < ARCHIVE_OK)
            return r;
        r = archive_write_data_block(aw, buff, size, offset);
        if (r < ARCHIVE_OK)
        {
            LOG_ERROR("Failed to write data block: {}",
                      archive_error_string(aw));
            return r;
        }
    }
}
inline bool extractTarArchive(const std::string& tarFilePath,
                              const std::string& directoryPath)
{
    struct archive* a;
    struct archive* ext;
    struct archive_entry* entry;
    int flags;
    int r;

    // Select which attributes we want to restore.
    flags = ARCHIVE_EXTRACT_TIME;
    flags |= ARCHIVE_EXTRACT_PERM;
    flags |= ARCHIVE_EXTRACT_ACL;
    flags |= ARCHIVE_EXTRACT_FFLAGS;

    a = archive_read_new();
    archive_read_support_format_tar(a);
    archive_read_support_filter_gzip(a);
    ext = archive_write_disk_new();
    archive_write_disk_set_options(ext, flags);
    archive_write_disk_set_standard_lookup(ext);

    if ((r = archive_read_open_filename(a, tarFilePath.c_str(), 10240)))
    {
        LOG_ERROR("Failed to open archive: {}", archive_error_string(a));
        return false;
    }

    while (true)
    {
        r = archive_read_next_header(a, &entry);
        if (r == ARCHIVE_EOF)
            break;
        if (r < ARCHIVE_OK)
            std::cerr << archive_error_string(a) << std::endl;
        if (r < ARCHIVE_WARN)
            return false;

        const char* currentFile = archive_entry_pathname(entry);
        std::string fullOutputPath = directoryPath + "/" + currentFile;
        archive_entry_set_pathname(entry, fullOutputPath.c_str());

        r = archive_write_header(ext, entry);
        if (r < ARCHIVE_OK)
            std::cerr << archive_error_string(ext) << std::endl;
        else if (archive_entry_size(entry) > 0)
        {
            r = copy_data(a, ext);
            if (r < ARCHIVE_OK)
                std::cerr << archive_error_string(ext) << std::endl;
            if (r < ARCHIVE_WARN)
                return false;
        }
        r = archive_write_finish_entry(ext);
        if (r < ARCHIVE_OK)
            std::cerr << archive_error_string(ext) << std::endl;
        if (r < ARCHIVE_WARN)
            return false;
    }

    archive_read_close(a);
    archive_read_free(a);
    archive_write_close(ext);
    archive_write_free(ext);

    return true;
}
