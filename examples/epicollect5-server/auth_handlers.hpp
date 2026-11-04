#pragma once
// auth_handlers.hpp — Authentication route registration for the
// Epicollect5-compatible server (Sub-Task 4).

#include "db.hpp"
#include "http_server.hpp"

namespace ec5
{

/// Register all authentication-related routes on `router`.
/// All handlers capture `db` by reference — the caller must ensure `db`
/// outlives the router.
void register_auth_routes(reactor::HttpRouter& router, ec5::Database& db);

} // namespace ec5
