#pragma once
// project_handlers.hpp — Project route registration for the
// Epicollect5-compatible server (Sub-Task 5).

#include "db.hpp"
#include "http_server.hpp"

namespace ec5
{

/// Register all project-related routes on `router`.
/// All handlers capture `db` by reference — the caller must ensure `db`
/// outlives the router.
void register_project_routes(reactor::HttpRouter& router, ec5::Database& db);

} // namespace ec5
