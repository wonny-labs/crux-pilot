/**
 * @file
 * @brief Private to the packaged web assets: the asset table, its headers and the content policy.
 *
 * Only the generated source of components/web/tools/web_assets.py, the handler in web_assets.c and
 * Crux's own tests include this. It is not on the include path of crux::web or of any project, and
 * no public header names it, so firmware-embedded data is not an application contract.
 *
 * The generated source defines CRUX_ASSETS_INLINE_STYLES, 0 for the strict policy or 1 for inline
 * styles, before it includes this header.
 */
#ifndef CRUX_WEB_ASSETS_INTERNAL_H
#define CRUX_WEB_ASSETS_INTERNAL_H

#include "crux_web_adapter.h"

#include <crux/web.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * The two content policies, and the only two: no policy text comes from a project. Scripts are
 * 'self' alone, so no inline script, eval or other origin runs, and frame-ancestors 'none' keeps
 * the interface out of every frame. The inline policy weakens style-src alone. base-uri is 'self'
 * because Angular's index.html carries a base element.
 */
#define CRUX_WEB_ASSETS_POLICY_STRICT                                                              \
    "default-src 'self'; script-src 'self'; style-src 'self'; base-uri 'self'; form-action "       \
    "'self'; frame-ancestors 'none'"
#define CRUX_WEB_ASSETS_POLICY_INLINE                                                              \
    "default-src 'self'; script-src 'self'; style-src 'self' 'unsafe-inline'; base-uri 'self'; "   \
    "form-action 'self'; frame-ancestors 'none'"

/* Only the generated source, which sets the choice, uses the policy. Without it the header list
 * below is undefined, so a source that forgot the choice does not compile. */
#ifdef CRUX_ASSETS_INLINE_STYLES
#if CRUX_ASSETS_INLINE_STYLES
#define CRUX_WEB_ASSETS_POLICY CRUX_WEB_ASSETS_POLICY_INLINE
#else
#define CRUX_WEB_ASSETS_POLICY CRUX_WEB_ASSETS_POLICY_STRICT
#endif

/** The three headers that every asset carries, after its Content-Type, encoding and cache header.
 */
#define CRUX_WEB_ASSETS_SECURITY_HEADERS                                                           \
    {"Content-Security-Policy", CRUX_WEB_ASSETS_POLICY}, {"X-Content-Type-Options", "nosniff"},    \
    {                                                                                              \
        "Referrer-Policy", "no-referrer"                                                           \
    }
#endif

/** One embedded asset. Its bytes are served as they are, already encoded. */
typedef struct {
    const char *path;
    const uint8_t *data;
    size_t length; /**< Bytes served, which are the compressed bytes for a gzip asset. */
    const crux_web_header_t *headers;
    size_t header_count;
} crux_web_asset_t;

/** Written by the packager, in the order of the paths. */
extern const crux_web_asset_t crux_web_asset_table[];
extern const size_t crux_web_asset_count;

/** The handler of every asset route. Its context is the asset's table entry. */
crux_web_result_t crux_web_assets_serve(crux_web_request_t *request, crux_access_session_t session,
                                        void *context);

#ifdef __cplusplus
}
#endif

#endif /* CRUX_WEB_ASSETS_INTERNAL_H */
