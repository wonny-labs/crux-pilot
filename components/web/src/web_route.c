/* The whole-path comparison with the route table. The server's resources only bring a request here:
 * FNM_LEADING_DIR makes them match deeper paths, so nothing is decided by which resource it came
 * on. */
#include "web_internal.h"

#include <string.h>

extern const crux_web_route_t *const crux_web_route_table[];
extern const crux_web_route_t crux_web_route_expected[];
extern const size_t crux_web_route_count;

/* Unreserved URL characters only. Nothing is decoded, so %2F or %2e%2e cannot become a path. */
static bool param_character(char character)
{
    return (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z') ||
           (character >= '0' && character <= '9') || character == '-' || character == '.' ||
           character == '_' || character == '~';
}

bool crux_web_route_matches(const char *pattern, const char *path, size_t path_length,
                            crux_web_request_t *request)
{
    size_t position = 0u;
    size_t params = 0u;

    while (*pattern != '\0') {
        if (*pattern == '{') {
            size_t start = position;

            while (position < path_length && path[position] != '/') {
                if (!param_character(path[position])) {
                    return false;
                }
                ++position;
            }
            if (position == start || params == CRUX_WEB_PATH_PARAMS_MAX ||
                (position - start == 1u && path[start] == '.') ||
                (position - start == 2u && path[start] == '.' && path[start + 1u] == '.')) {
                return false;
            }
            request->params[params].offset = start;
            request->params[params].length = position - start;
            ++params;
            pattern = strchr(pattern, '}');
            if (pattern == NULL) {
                return false;
            }
            ++pattern;
        } else {
            if (position >= path_length || path[position] != *pattern) {
                return false;
            }
            ++position;
            ++pattern;
        }
    }
    if (position != path_length) {
        return false;
    }
    request->param_count = params;
    return true;
}

bool crux_web_routes_check(const crux_web_route_t *const *table, const crux_web_route_t *expected,
                           size_t count)
{
    for (size_t index = 0; index < count; ++index) {
        const crux_web_route_t *route = table[index];

        if (route->path == NULL || route->path[0] != '/' || route->access == NULL ||
            route->access[0] == '\0' || route->handler == NULL || route->methods == 0u ||
            (route->methods & ~CRUX_WEB_METHODS_ALL) != 0u ||
            route->body_max > CRUX_WEB_BODY_BUFFER) {
            return false;
        }
        if (strcmp(route->path, expected[index].path) != 0 ||
            strcmp(route->access, expected[index].access) != 0 ||
            route->methods != expected[index].methods) {
            return false;
        }
    }
    return true;
}

bool crux_web_routes_valid(void)
{
    return crux_web_routes_check(crux_web_route_table, crux_web_route_expected,
                                 crux_web_route_count);
}

const crux_web_route_t *crux_web_route_find(const char *path, size_t path_length, unsigned method,
                                            crux_web_request_t *request)
{
    bool fixed_path = false;

#ifdef CRUX_WEB_TESTING
    if (crux_web_core_test_route != NULL) {
        return crux_web_core_test_route;
    }
#endif
    request->param_count = 0u;
    for (size_t index = 0; index < crux_web_route_count; ++index) {
        const crux_web_route_t *route = crux_web_route_table[index];

        if (strchr(route->path, '{') == NULL &&
            crux_web_route_matches(route->path, path, path_length, request)) {
            if ((route->methods & method) != 0u) {
                return route;
            }
            fixed_path = true;
        }
    }
    if (fixed_path) {
        return NULL;
    }
    for (size_t index = 0; index < crux_web_route_count; ++index) {
        const crux_web_route_t *route = crux_web_route_table[index];

        if (strchr(route->path, '{') != NULL && (route->methods & method) != 0u &&
            crux_web_route_matches(route->path, path, path_length, request)) {
            return route;
        }
    }
    return NULL;
}
