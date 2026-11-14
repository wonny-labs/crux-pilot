/* A composition that leaves the reboot service out has no reboot route. This core is built from the
 * same sources as the route suites' core, except that its route table does not name the reboot
 * source, and the service is started all the same: nothing about the service puts a route in. */
#include "reboot_web.h"

#include <crux/web.h>

extern const crux_web_route_t *const crux_web_route_table[];
extern const size_t crux_web_route_count;

static void the_route_table_has_no_reboot_route(void)
{
    CHECK(crux_web_route_count > 0u);
    for (size_t index = 0; index < crux_web_route_count; ++index) {
        const crux_web_route_t *route = crux_web_route_table[index];

        if (strstr(route->path, "reboot") != NULL || strstr(route->path, "/system") != NULL ||
            strcmp(route->access, CRUX_REBOOT_PERMISSION) == 0) {
            printf("  unexpected route %s\n", route->path);
            CHECK(false);
        }
    }
}

static void a_request_for_it_finds_nothing(void)
{
    const session_jar_t *jar;
    const fake_reply_t *reply;

    web_reboot_setup();
    jar = web_reboot_jar("admin");
    /* The administrator holds the permission, and the service is running, and there is still no
     * route: nothing runs, nothing is recorded, and nothing is asked of the policy. */
    reply = web_reboot_get(jar);
    CHECK(reply->status == 404);
    reply = web_reboot_post(jar);
    CHECK(reply->status == 404);
    CHECK(reboot_trace_count() == 0u && reboot_policy_calls() == 0u);
    CHECK(!crux_reboot_work_once());
}

int main(void)
{
    reboot_run_case("the route table has no reboot route", the_route_table_has_no_reboot_route);
    reboot_run_case("a request for it finds nothing", a_request_for_it_finds_nothing);
    return reboot_finish("web_reboot_no_service");
}
