#pragma once
// admin_handlers.hpp — HTML admin UI routes for the Epicollect5-compatible
// server.  Provides a browser-based project creation interface at GET /admin.

#include "db.hpp"
#include "http_server.hpp"

namespace ec5
{

/// Register all admin UI routes on `router`.
void register_admin_routes(reactor::HttpRouter& router, ec5::Database& db);

} // namespace ec5
