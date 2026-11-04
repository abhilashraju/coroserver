#pragma once
// db.hpp — SQLite database wrapper for the Epicollect5-compatible server.
// All public methods are synchronous (sqlite3 is called directly; no async).

#include "sqlite3.h"

#include <optional>
#include <string>
#include <vector>

namespace ec5
{

// ---------------------------------------------------------------------------
// Row types returned by query helpers
// ---------------------------------------------------------------------------

struct UserRow
{
    int id{0};
    std::string email;
    std::string passwordHash; // "salt:sha256hex"
    std::string name;
    std::string createdAt;
};

struct ProjectRow
{
    int id{0};
    std::string slug;
    std::string name;
    std::string ref; // UUID
    std::string definitionJson;
    std::string structureLastUpdated;
};

struct EntryRow
{
    std::string uuid;
    std::string projectSlug;
    std::string formRef;
    std::string parentUuid;
    std::string platform;
    std::string title;
    std::string answersJson;
    std::string createdAt;
    int isBranch{0};
    std::string branchRef;
    std::string branchOwnerUuid;
};

struct MediaRow
{
    int id{0};
    std::string entryUuid;
    std::string projectSlug;
    std::string filename;
    std::string filetype;
    std::string storagePath;
    int isTemp{0};
};

struct CodeRow
{
    std::string email;
    std::string code;
    std::string expiresAt; // ISO-8601 UTC string
};

// ---------------------------------------------------------------------------
// Database — thin wrapper around a sqlite3* handle
// ---------------------------------------------------------------------------

class Database
{
  public:
    Database() = default;
    ~Database();

    // Not copyable
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Open (or create) the SQLite file at `path` and run CREATE TABLE IF NOT
    // EXISTS for all tables.  Throws std::runtime_error on failure.
    void openOrCreate(const std::string& path);

    // ------------------------------------------------------------------
    // users
    // ------------------------------------------------------------------

    // Hash `plainPassword` and insert a new user row.
    // Returns the new row's id, or throws on constraint violation.
    int insertUser(const std::string& email, const std::string& plainPassword,
                   const std::string& name);

    // Returns the user row matching `email`, or std::nullopt if not found.
    std::optional<UserRow> findUserByEmail(const std::string& email);

    // Returns true if `plainPassword` matches the stored hash for `email`.
    bool verifyUserPassword(const std::string& email,
                            const std::string& plainPassword);

    // ------------------------------------------------------------------
    // projects
    // ------------------------------------------------------------------

    // Insert a new project. The caller provides the full definition JSON.
    // Returns the new row's id.
    int insertProject(const std::string& slug, const std::string& name,
                      const std::string& ref,
                      const std::string& definitionJson,
                      const std::string& structureLastUpdated);

    // Insert or replace a project (upsert on slug).
    // Returns the row id of the inserted/updated row.
    int upsertProject(const std::string& slug, const std::string& name,
                      const std::string& ref,
                      const std::string& definitionJson,
                      const std::string& structureLastUpdated);

    // Returns the project row matching `slug`, or std::nullopt.
    std::optional<ProjectRow> findProjectBySlug(const std::string& slug);

    // Returns every project row ordered by name.
    std::vector<ProjectRow> listAllProjects();

    // Returns all projects whose name or slug contains `term` (LIKE).
    // If `exact` is true, requires an exact match on slug or name.
    std::vector<ProjectRow> searchProjects(const std::string& term,
                                           bool exact = false);

    // ------------------------------------------------------------------
    // entries
    // ------------------------------------------------------------------

    // Insert an entry row. `answersJson` is the raw JSON blob from the
    // mobile payload.
    void insertEntry(const EntryRow& row);

    // Returns entries for a given project + form, paginated.
    // `page` is 1-based; `perPage` defaults to 50.
    std::vector<EntryRow> findEntriesByForm(const std::string& projectSlug,
                                            const std::string& formRef,
                                            int page = 1,
                                            int perPage = 50);

    // Returns the single entry with `uuid`, or std::nullopt.
    std::optional<EntryRow> findEntryByUuid(const std::string& uuid);

    // Returns all distinct answer values stored for a particular `inputRef`
    // across all entries in `projectSlug` + `formRef`.
    std::vector<std::string> findAnswersByInputRef(
        const std::string& projectSlug, const std::string& formRef,
        const std::string& inputRef, int page = 1, int perPage = 50);

    // ------------------------------------------------------------------
    // media_files
    // ------------------------------------------------------------------

    // Insert a media file record.  `isTemp` should be true while the file
    // has not yet been committed to a permanent entry.
    void insertMedia(const std::string& entryUuid,
                     const std::string& projectSlug,
                     const std::string& filename, const std::string& filetype,
                     const std::string& storagePath, bool isTemp);

    // Returns the media row matching project + filename + filetype, or
    // std::nullopt.
    std::optional<MediaRow> findMedia(const std::string& projectSlug,
                                      const std::string& filename,
                                      const std::string& filetype);

    // Deletes the media row matching project + filename + filetype.
    void deleteMedia(const std::string& projectSlug,
                     const std::string& filename, const std::string& filetype);

    // ------------------------------------------------------------------
    // passwordless_codes
    // ------------------------------------------------------------------

    // Inserts (or replaces) a passwordless code for `email`.
    void insertCode(const std::string& email, const std::string& code,
                    const std::string& expiresAt);

    // Returns the code row for `email`, or std::nullopt.
    std::optional<CodeRow> findCode(const std::string& email);

    // Deletes all code rows for `email`.
    void deleteCode(const std::string& email);

  private:
    sqlite3* db_{nullptr};

    // Execute a SQL string that produces no result rows.
    void exec(const char* sql);

    // Create all tables.
    void createSchema();

    // Password helpers — SHA-256 + random hex salt.
    static std::string hashPassword(const std::string& plain,
                                    const std::string& salt);
    static std::string generateSalt();
    // Returns "salt:hash" string.
    static std::string makePasswordHash(const std::string& plain);
    // Returns true if plain matches the stored "salt:hash" token.
    static bool checkPassword(const std::string& plain,
                               const std::string& stored);
};

} // namespace ec5
