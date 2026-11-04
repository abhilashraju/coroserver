#pragma once
// entry_handlers.hpp — Entry upload/download route registration for the
// Epicollect5-compatible server (Sub-Task 6).

#include "db.hpp"
#include "http_server.hpp"

namespace ec5
{

/// Register all entry-related routes on `router`.
/// All handlers capture `db` by reference — the caller must ensure `db`
/// outlives the router.
void register_entry_routes(reactor::HttpRouter& router, ec5::Database& db);

} // namespace ec5
