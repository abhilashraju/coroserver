// db.cpp — SQLite database layer for the Epicollect5-compatible server.
//
// Password hashing uses SHA-256 + a random 16-byte hex salt via the OpenSSL
// EVP_Digest API (bcrypt is not available in the build environment).

#include "db.hpp"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <array>
#include <cstring>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace ec5
{

// ---------------------------------------------------------------------------
// RAII helper for prepared statements
// ---------------------------------------------------------------------------

struct Stmt
{
    sqlite3_stmt* ptr{nullptr};
    ~Stmt()
    {
        sqlite3_finalize(ptr);
    }
    // Prepare a statement; throws on error.
    void prepare(sqlite3* db, const char* sql)
    {
        if (sqlite3_prepare_v2(db, sql, -1, &ptr, nullptr) != SQLITE_OK)
        {
            throw std::runtime_error(
                std::string("sqlite3_prepare_v2: ") + sqlite3_errmsg(db));
        }
    }
};

// ---------------------------------------------------------------------------
// Helper: bind a std::string as text (allows empty strings)
// ---------------------------------------------------------------------------

static void bindText(sqlite3_stmt* stmt, int col, const std::string& val)
{
    sqlite3_bind_text(stmt, col, val.c_str(), static_cast<int>(val.size()),
                      SQLITE_TRANSIENT);
}

static std::string colText(sqlite3_stmt* stmt, int col)
{
    const unsigned char* v = sqlite3_column_text(stmt, col);
    return v ? reinterpret_cast<const char*>(v) : std::string{};
}

// ---------------------------------------------------------------------------
// Database destructor
// ---------------------------------------------------------------------------

Database::~Database()
{
    if (db_)
    {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

// ---------------------------------------------------------------------------
// exec — run SQL that returns no rows
// ---------------------------------------------------------------------------

void Database::exec(const char* sql)
{
    char* errmsg = nullptr;
    if (sqlite3_exec(db_, sql, nullptr, nullptr, &errmsg) != SQLITE_OK)
    {
        std::string msg = errmsg ? errmsg : "unknown error";
        sqlite3_free(errmsg);
        throw std::runtime_error(std::string("sqlite3_exec: ") + msg);
    }
}

// ---------------------------------------------------------------------------
// createSchema — CREATE TABLE IF NOT EXISTS for every table
// ---------------------------------------------------------------------------

void Database::createSchema()
{
    exec(R"(
        CREATE TABLE IF NOT EXISTS users (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            email         TEXT    NOT NULL UNIQUE,
            password_hash TEXT    NOT NULL,
            name          TEXT    NOT NULL DEFAULT '',
            created_at    TEXT    NOT NULL DEFAULT (datetime('now'))
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS projects (
            id                      INTEGER PRIMARY KEY AUTOINCREMENT,
            slug                    TEXT    NOT NULL UNIQUE,
            name                    TEXT    NOT NULL,
            ref                     TEXT    NOT NULL,
            definition_json         TEXT    NOT NULL DEFAULT '{}',
            structure_last_updated  TEXT    NOT NULL DEFAULT (datetime('now'))
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS entries (
            uuid               TEXT PRIMARY KEY,
            project_slug       TEXT NOT NULL,
            form_ref           TEXT NOT NULL,
            parent_uuid        TEXT NOT NULL DEFAULT '',
            platform           TEXT NOT NULL DEFAULT '',
            title              TEXT NOT NULL DEFAULT '',
            answers_json       TEXT NOT NULL DEFAULT '{}',
            created_at         TEXT NOT NULL DEFAULT (datetime('now')),
            is_branch          INTEGER NOT NULL DEFAULT 0,
            branch_ref         TEXT NOT NULL DEFAULT '',
            branch_owner_uuid  TEXT NOT NULL DEFAULT ''
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS media_files (
            id            INTEGER PRIMARY KEY AUTOINCREMENT,
            entry_uuid    TEXT    NOT NULL DEFAULT '',
            project_slug  TEXT    NOT NULL,
            filename      TEXT    NOT NULL,
            filetype      TEXT    NOT NULL,
            storage_path  TEXT    NOT NULL,
            is_temp       INTEGER NOT NULL DEFAULT 1
        );
    )");

    exec(R"(
        CREATE TABLE IF NOT EXISTS passwordless_codes (
            email       TEXT PRIMARY KEY,
            code        TEXT NOT NULL,
            expires_at  TEXT NOT NULL
        );
    )");
}

// ---------------------------------------------------------------------------
// openOrCreate
// ---------------------------------------------------------------------------

void Database::openOrCreate(const std::string& path)
{
    if (sqlite3_open(path.c_str(), &db_) != SQLITE_OK)
    {
        std::string msg = db_ ? sqlite3_errmsg(db_) : "cannot open database";
        throw std::runtime_error("Database::openOrCreate: " + msg);
    }
    // Enable WAL for better concurrent read performance
    exec("PRAGMA journal_mode=WAL;");
    exec("PRAGMA foreign_keys=ON;");
    createSchema();
}

// ---------------------------------------------------------------------------
// Password helpers
// ---------------------------------------------------------------------------

// Returns a 32-hex-char (16-byte) random salt string.
std::string Database::generateSalt()
{
    std::array<unsigned char, 16> buf{};
    if (RAND_bytes(buf.data(), static_cast<int>(buf.size())) != 1)
    {
        throw std::runtime_error("RAND_bytes failed");
    }
    std::ostringstream oss;
    for (unsigned char c : buf)
    {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(c);
    }
    return oss.str();
}

// SHA-256 of (salt + plain), returned as a lowercase hex string.
std::string Database::hashPassword(const std::string& plain,
                                   const std::string& salt)
{
    std::string input = salt + plain;

    unsigned char digest[EVP_MAX_MD_SIZE];
    unsigned int digestLen = 0;

    if (EVP_Digest(input.data(), input.size(), digest, &digestLen,
                   EVP_sha256(), nullptr) != 1)
    {
        throw std::runtime_error("EVP_Digest (SHA-256) failed");
    }

    std::ostringstream oss;
    for (unsigned int i = 0; i < digestLen; ++i)
    {
        oss << std::hex << std::setw(2) << std::setfill('0')
            << static_cast<int>(digest[i]);
    }
    return oss.str();
}

// Returns "salt:sha256hex".
std::string Database::makePasswordHash(const std::string& plain)
{
    std::string salt = generateSalt();
    std::string hash = hashPassword(plain, salt);
    return salt + ":" + hash;
}

// `stored` is "salt:sha256hex"; returns true if plain matches.
bool Database::checkPassword(const std::string& plain,
                              const std::string& stored)
{
    auto sep = stored.find(':');
    if (sep == std::string::npos)
    {
        return false;
    }
    std::string salt = stored.substr(0, sep);
    std::string expected = stored.substr(sep + 1);
    std::string actual = hashPassword(plain, salt);
    // Constant-time comparison to resist timing attacks
    if (actual.size() != expected.size())
    {
        return false;
    }
    unsigned char diff = 0;
    for (std::size_t i = 0; i < actual.size(); ++i)
    {
        diff |= static_cast<unsigned char>(actual[i]) ^
                static_cast<unsigned char>(expected[i]);
    }
    return diff == 0;
}

// ---------------------------------------------------------------------------
// users
// ---------------------------------------------------------------------------

int Database::insertUser(const std::string& email,
                         const std::string& plainPassword,
                         const std::string& name)
{
    std::string hash = makePasswordHash(plainPassword);

    Stmt stmt;
    stmt.prepare(db_,
                 "INSERT INTO users (email, password_hash, name) "
                 "VALUES (?, ?, ?);");
    bindText(stmt.ptr, 1, email);
    bindText(stmt.ptr, 2, hash);
    bindText(stmt.ptr, 3, name);

    if (sqlite3_step(stmt.ptr) != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("insertUser: ") +
                                 sqlite3_errmsg(db_));
    }
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

std::optional<UserRow> Database::findUserByEmail(const std::string& email)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "SELECT id, email, password_hash, name, created_at FROM users "
        "WHERE email = ? LIMIT 1;");
    bindText(stmt.ptr, 1, email);

    if (sqlite3_step(stmt.ptr) != SQLITE_ROW)
    {
        return std::nullopt;
    }

    UserRow row;
    row.id = sqlite3_column_int(stmt.ptr, 0);
    row.email = colText(stmt.ptr, 1);
    row.passwordHash = colText(stmt.ptr, 2);
    row.name = colText(stmt.ptr, 3);
    row.createdAt = colText(stmt.ptr, 4);
    return row;
}

bool Database::verifyUserPassword(const std::string& email,
                                  const std::string& plainPassword)
{
    auto user = findUserByEmail(email);
    if (!user)
    {
        return false;
    }
    return checkPassword(plainPassword, user->passwordHash);
}

// ---------------------------------------------------------------------------
// projects
// ---------------------------------------------------------------------------

int Database::insertProject(const std::string& slug, const std::string& name,
                             const std::string& ref,
                             const std::string& definitionJson,
                             const std::string& structureLastUpdated)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "INSERT INTO projects "
        "(slug, name, ref, definition_json, structure_last_updated) "
        "VALUES (?, ?, ?, ?, ?);");
    bindText(stmt.ptr, 1, slug);
    bindText(stmt.ptr, 2, name);
    bindText(stmt.ptr, 3, ref);
    bindText(stmt.ptr, 4, definitionJson);
    bindText(stmt.ptr, 5, structureLastUpdated);

    if (sqlite3_step(stmt.ptr) != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("insertProject: ") +
                                 sqlite3_errmsg(db_));
    }
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

int Database::upsertProject(const std::string& slug, const std::string& name,
                             const std::string& ref,
                             const std::string& definitionJson,
                             const std::string& structureLastUpdated)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "INSERT INTO projects "
        "(slug, name, ref, definition_json, structure_last_updated) "
        "VALUES (?, ?, ?, ?, ?) "
        "ON CONFLICT(slug) DO UPDATE SET "
        "  name                  = excluded.name, "
        "  ref                   = excluded.ref, "
        "  definition_json       = excluded.definition_json, "
        "  structure_last_updated = excluded.structure_last_updated;");
    bindText(stmt.ptr, 1, slug);
    bindText(stmt.ptr, 2, name);
    bindText(stmt.ptr, 3, ref);
    bindText(stmt.ptr, 4, definitionJson);
    bindText(stmt.ptr, 5, structureLastUpdated);

    if (sqlite3_step(stmt.ptr) != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("upsertProject: ") +
                                 sqlite3_errmsg(db_));
    }
    return static_cast<int>(sqlite3_last_insert_rowid(db_));
}

std::optional<ProjectRow> Database::findProjectBySlug(const std::string& slug)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "SELECT id, slug, name, ref, definition_json, structure_last_updated "
        "FROM projects WHERE slug = ? LIMIT 1;");
    bindText(stmt.ptr, 1, slug);

    if (sqlite3_step(stmt.ptr) != SQLITE_ROW)
    {
        return std::nullopt;
    }

    ProjectRow row;
    row.id = sqlite3_column_int(stmt.ptr, 0);
    row.slug = colText(stmt.ptr, 1);
    row.name = colText(stmt.ptr, 2);
    row.ref = colText(stmt.ptr, 3);
    row.definitionJson = colText(stmt.ptr, 4);
    row.structureLastUpdated = colText(stmt.ptr, 5);
    return row;
}

std::vector<ProjectRow> Database::listAllProjects()
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "SELECT id, slug, name, ref, definition_json, structure_last_updated "
        "FROM projects ORDER BY name;");

    std::vector<ProjectRow> rows;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        ProjectRow row;
        row.id = sqlite3_column_int(stmt.ptr, 0);
        row.slug = colText(stmt.ptr, 1);
        row.name = colText(stmt.ptr, 2);
        row.ref = colText(stmt.ptr, 3);
        row.definitionJson = colText(stmt.ptr, 4);
        row.structureLastUpdated = colText(stmt.ptr, 5);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::vector<ProjectRow> Database::searchProjects(const std::string& term,
                                                  bool exact)
{
    Stmt stmt;
    if (exact)
    {
        stmt.prepare(
            db_,
            "SELECT id, slug, name, ref, definition_json, "
            "structure_last_updated FROM projects "
            "WHERE slug = ? OR name = ?;");
        bindText(stmt.ptr, 1, term);
        bindText(stmt.ptr, 2, term);
    }
    else
    {
        std::string pattern = "%" + term + "%";
        stmt.prepare(
            db_,
            "SELECT id, slug, name, ref, definition_json, "
            "structure_last_updated FROM projects "
            "WHERE slug LIKE ? OR name LIKE ?;");
        bindText(stmt.ptr, 1, pattern);
        bindText(stmt.ptr, 2, pattern);
    }

    std::vector<ProjectRow> rows;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        ProjectRow row;
        row.id = sqlite3_column_int(stmt.ptr, 0);
        row.slug = colText(stmt.ptr, 1);
        row.name = colText(stmt.ptr, 2);
        row.ref = colText(stmt.ptr, 3);
        row.definitionJson = colText(stmt.ptr, 4);
        row.structureLastUpdated = colText(stmt.ptr, 5);
        rows.push_back(std::move(row));
    }
    return rows;
}

// ---------------------------------------------------------------------------
// entries
// ---------------------------------------------------------------------------

void Database::insertEntry(const EntryRow& row)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "INSERT OR REPLACE INTO entries "
        "(uuid, project_slug, form_ref, parent_uuid, platform, title, "
        " answers_json, created_at, is_branch, branch_ref, branch_owner_uuid) "
        "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?);");

    bindText(stmt.ptr, 1, row.uuid);
    bindText(stmt.ptr, 2, row.projectSlug);
    bindText(stmt.ptr, 3, row.formRef);
    bindText(stmt.ptr, 4, row.parentUuid);
    bindText(stmt.ptr, 5, row.platform);
    bindText(stmt.ptr, 6, row.title);
    bindText(stmt.ptr, 7, row.answersJson);
    bindText(stmt.ptr, 8, row.createdAt);
    sqlite3_bind_int(stmt.ptr, 9, row.isBranch);
    bindText(stmt.ptr, 10, row.branchRef);
    bindText(stmt.ptr, 11, row.branchOwnerUuid);

    if (sqlite3_step(stmt.ptr) != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("insertEntry: ") +
                                 sqlite3_errmsg(db_));
    }
}

std::vector<EntryRow> Database::findEntriesByForm(
    const std::string& projectSlug, const std::string& formRef, int page,
    int perPage)
{
    int offset = (page - 1) * perPage;

    Stmt stmt;
    stmt.prepare(
        db_,
        "SELECT uuid, project_slug, form_ref, parent_uuid, platform, title, "
        "       answers_json, created_at, is_branch, branch_ref, "
        "       branch_owner_uuid "
        "FROM entries "
        "WHERE project_slug = ? AND form_ref = ? "
        "ORDER BY created_at DESC "
        "LIMIT ? OFFSET ?;");

    bindText(stmt.ptr, 1, projectSlug);
    bindText(stmt.ptr, 2, formRef);
    sqlite3_bind_int(stmt.ptr, 3, perPage);
    sqlite3_bind_int(stmt.ptr, 4, offset);

    std::vector<EntryRow> rows;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        EntryRow row;
        row.uuid = colText(stmt.ptr, 0);
        row.projectSlug = colText(stmt.ptr, 1);
        row.formRef = colText(stmt.ptr, 2);
        row.parentUuid = colText(stmt.ptr, 3);
        row.platform = colText(stmt.ptr, 4);
        row.title = colText(stmt.ptr, 5);
        row.answersJson = colText(stmt.ptr, 6);
        row.createdAt = colText(stmt.ptr, 7);
        row.isBranch = sqlite3_column_int(stmt.ptr, 8);
        row.branchRef = colText(stmt.ptr, 9);
        row.branchOwnerUuid = colText(stmt.ptr, 10);
        rows.push_back(std::move(row));
    }
    return rows;
}

std::optional<EntryRow> Database::findEntryByUuid(const std::string& uuid)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "SELECT uuid, project_slug, form_ref, parent_uuid, platform, title, "
        "       answers_json, created_at, is_branch, branch_ref, "
        "       branch_owner_uuid "
        "FROM entries WHERE uuid = ? LIMIT 1;");
    bindText(stmt.ptr, 1, uuid);

    if (sqlite3_step(stmt.ptr) != SQLITE_ROW)
    {
        return std::nullopt;
    }

    EntryRow row;
    row.uuid = colText(stmt.ptr, 0);
    row.projectSlug = colText(stmt.ptr, 1);
    row.formRef = colText(stmt.ptr, 2);
    row.parentUuid = colText(stmt.ptr, 3);
    row.platform = colText(stmt.ptr, 4);
    row.title = colText(stmt.ptr, 5);
    row.answersJson = colText(stmt.ptr, 6);
    row.createdAt = colText(stmt.ptr, 7);
    row.isBranch = sqlite3_column_int(stmt.ptr, 8);
    row.branchRef = colText(stmt.ptr, 9);
    row.branchOwnerUuid = colText(stmt.ptr, 10);
    return row;
}

std::vector<std::string> Database::findAnswersByInputRef(
    const std::string& projectSlug, const std::string& formRef,
    const std::string& inputRef, int page, int perPage)
{
    // answers_json is stored as a JSON object keyed by input_ref.
    // We use a simple LIKE to find entries that mention the input_ref,
    // then extract the value with the json_extract SQLite JSON function.
    int offset = (page - 1) * perPage;

    std::string jsonPath = "$." + inputRef;

    Stmt stmt;
    stmt.prepare(
        db_,
        "SELECT DISTINCT json_extract(answers_json, ?) "
        "FROM entries "
        "WHERE project_slug = ? AND form_ref = ? "
        "  AND json_extract(answers_json, ?) IS NOT NULL "
        "LIMIT ? OFFSET ?;");

    bindText(stmt.ptr, 1, jsonPath);
    bindText(stmt.ptr, 2, projectSlug);
    bindText(stmt.ptr, 3, formRef);
    bindText(stmt.ptr, 4, jsonPath);
    sqlite3_bind_int(stmt.ptr, 5, perPage);
    sqlite3_bind_int(stmt.ptr, 6, offset);

    std::vector<std::string> answers;
    while (sqlite3_step(stmt.ptr) == SQLITE_ROW)
    {
        answers.push_back(colText(stmt.ptr, 0));
    }
    return answers;
}

// ---------------------------------------------------------------------------
// media_files
// ---------------------------------------------------------------------------

void Database::insertMedia(const std::string& entryUuid,
                           const std::string& projectSlug,
                           const std::string& filename,
                           const std::string& filetype,
                           const std::string& storagePath, bool isTemp)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "INSERT INTO media_files "
        "(entry_uuid, project_slug, filename, filetype, storage_path, is_temp) "
        "VALUES (?, ?, ?, ?, ?, ?);");
    bindText(stmt.ptr, 1, entryUuid);
    bindText(stmt.ptr, 2, projectSlug);
    bindText(stmt.ptr, 3, filename);
    bindText(stmt.ptr, 4, filetype);
    bindText(stmt.ptr, 5, storagePath);
    sqlite3_bind_int(stmt.ptr, 6, isTemp ? 1 : 0);

    if (sqlite3_step(stmt.ptr) != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("insertMedia: ") +
                                 sqlite3_errmsg(db_));
    }
}

std::optional<MediaRow> Database::findMedia(const std::string& projectSlug,
                                             const std::string& filename,
                                             const std::string& filetype)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "SELECT id, entry_uuid, project_slug, filename, filetype, "
        "       storage_path, is_temp "
        "FROM media_files "
        "WHERE project_slug = ? AND filename = ? AND filetype = ? LIMIT 1;");
    bindText(stmt.ptr, 1, projectSlug);
    bindText(stmt.ptr, 2, filename);
    bindText(stmt.ptr, 3, filetype);

    if (sqlite3_step(stmt.ptr) != SQLITE_ROW)
    {
        return std::nullopt;
    }

    MediaRow row;
    row.id = sqlite3_column_int(stmt.ptr, 0);
    row.entryUuid = colText(stmt.ptr, 1);
    row.projectSlug = colText(stmt.ptr, 2);
    row.filename = colText(stmt.ptr, 3);
    row.filetype = colText(stmt.ptr, 4);
    row.storagePath = colText(stmt.ptr, 5);
    row.isTemp = sqlite3_column_int(stmt.ptr, 6);
    return row;
}

void Database::deleteMedia(const std::string& projectSlug,
                            const std::string& filename,
                            const std::string& filetype)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "DELETE FROM media_files "
        "WHERE project_slug = ? AND filename = ? AND filetype = ?;");
    bindText(stmt.ptr, 1, projectSlug);
    bindText(stmt.ptr, 2, filename);
    bindText(stmt.ptr, 3, filetype);

    if (sqlite3_step(stmt.ptr) != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("deleteMedia: ") +
                                 sqlite3_errmsg(db_));
    }
}

// ---------------------------------------------------------------------------
// passwordless_codes
// ---------------------------------------------------------------------------

void Database::insertCode(const std::string& email, const std::string& code,
                           const std::string& expiresAt)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "INSERT OR REPLACE INTO passwordless_codes (email, code, expires_at) "
        "VALUES (?, ?, ?);");
    bindText(stmt.ptr, 1, email);
    bindText(stmt.ptr, 2, code);
    bindText(stmt.ptr, 3, expiresAt);

    if (sqlite3_step(stmt.ptr) != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("insertCode: ") +
                                 sqlite3_errmsg(db_));
    }
}

std::optional<CodeRow> Database::findCode(const std::string& email)
{
    Stmt stmt;
    stmt.prepare(
        db_,
        "SELECT email, code, expires_at FROM passwordless_codes "
        "WHERE email = ? LIMIT 1;");
    bindText(stmt.ptr, 1, email);

    if (sqlite3_step(stmt.ptr) != SQLITE_ROW)
    {
        return std::nullopt;
    }

    CodeRow row;
    row.email = colText(stmt.ptr, 0);
    row.code = colText(stmt.ptr, 1);
    row.expiresAt = colText(stmt.ptr, 2);
    return row;
}

void Database::deleteCode(const std::string& email)
{
    Stmt stmt;
    stmt.prepare(db_,
                 "DELETE FROM passwordless_codes WHERE email = ?;");
    bindText(stmt.ptr, 1, email);

    if (sqlite3_step(stmt.ptr) != SQLITE_DONE)
    {
        throw std::runtime_error(std::string("deleteCode: ") +
                                 sqlite3_errmsg(db_));
    }
}

} // namespace ec5
