// =============================================================================
// SoleSense v0.2 -- http_routes.h
// AsyncWebServer route registration.
// =============================================================================

#pragma once

#include <ESPAsyncWebServer.h>

// Register all HTTP routes on the given server. Call once from setup() before
// server.begin().
void http_register_routes(AsyncWebServer& server);
