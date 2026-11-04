#pragma once
// media_handlers.hpp — Media file upload, delete, and serve routes for the
// Epicollect5-compatible server (Sub-Task 7).

#include "db.hpp"
#include "http_server.hpp"

namespace ec5
{

/// Register all media-related routes on `router`.
/// All handlers capture `db` by reference — the caller must ensure `db`
/// outlives the router.
void register_media_routes(reactor::HttpRouter& router, ec5::Database& db);

} // namespace ec5
