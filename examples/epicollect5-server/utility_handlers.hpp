#pragma once
// utility_handlers.hpp — Utility routes (uniqueness validation & OpenCage proxy
// stub) for the Epicollect5-compatible server (Sub-Task 8).

#include "db.hpp"
#include "http_server.hpp"

namespace ec5
{

/// Register utility routes on `router`.
/// Handlers capture `db` by reference — the caller must ensure `db` outlives
/// the router.
void register_utility_routes(reactor::HttpRouter& router, ec5::Database& db);

} // namespace ec5
