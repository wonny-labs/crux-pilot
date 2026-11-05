/* PUT on a device with no Configuration provider, which is the target until #16: the
 * specification's 501, after the rules and before anything is queued. */
#include "network_web.h"

static void put_says_not_supported(void)
{
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    web_net_expect_error(web_net_put(jar, "{\"address\":\"198.51.100.20\",\"prefix_length\":24}"),
                         501, "not_supported");
    /* The rules still answer first, so that a client learns what is wrong with its values. */
    web_net_expect_body(web_net_put(jar, "{\"address\":\"224.0.0.1\",\"prefix_length\":24}"), 400,
                        "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"address\",\"code\":"
                        "\"not_unicast\"}]}");
    web_net_expect_error(web_net_put(jar, "{"), 400, "bad_request");
    web_net_expect_error(
        web_net_put(web_net_jar("reader"), "{\"address\":\"198.51.100.20\",\"prefix_length\":24}"),
        403, "permission_denied");
    CHECK(!crux_network_settings_work_once());
    /* Reading still works, and shows the defaults. */
    web_net_expect_body(web_net_get(jar), 200,
                        "{\"running\":{\"address\":\"192.0.2.10\",\"prefix_length\":24},"
                        "\"source\":\"default\",\"reboot_required\":false}");
}

int main(void)
{
    net_run_case("PUT says not supported", put_says_not_supported);
    return net_finish("web_network_no_provider");
}
