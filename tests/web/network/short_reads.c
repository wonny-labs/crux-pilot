/* The body of PUT /api/v1/network when the core hands it over a few bytes at a time. The contract
 * of crux_web_read_body() is to copy the next bytes, and the real core copies them all at once, so
 * a handler that read once would work on every other suite. The read is wrapped here to give at
 * most a set number of bytes a call, and the answers must not depend on that number. */
#include "network_web.h"

// NOLINTBEGIN(bugprone-reserved-identifier): GNU linker wrapping ABI
crux_web_result_t __real_crux_web_read_body(crux_web_request_t *request, void *buffer,
                                            size_t length, size_t *transferred);
crux_web_result_t __wrap_crux_web_read_body(crux_web_request_t *request, void *buffer,
                                            size_t length, size_t *transferred);
// NOLINTEND(bugprone-reserved-identifier)

static size_t piece;   /* Most bytes a call returns, or 0 for no limit. */
static unsigned reads; /* Calls that the handler made. */

crux_web_result_t __wrap_crux_web_read_body(crux_web_request_t *request, void *buffer,
                                            size_t length, size_t *transferred)
{
    ++reads;
    if (piece != 0u && length > piece) {
        length = piece;
    }
    return __real_crux_web_read_body(request, buffer, length, transferred);
}

#define SET_A "{\"address\":\"198.51.100.20\",\"prefix_length\":24,\"gateway\":\"198.51.100.1\"}"

static void answers_do_not_depend_on_the_piece(void)
{
    static const size_t pieces[] = {0u, 1u, 2u, 3u, 7u, 16u, 255u, 256u};
    static const char *const bodies[] = {
        SET_A,
        "{\"address\":\"203.0.113.20\",\"prefix_length\":26}",
        "{\"address\":\"224.0.0.1\",\"prefix_length\":31}",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24",
        "{\"address\":\"192.0.2.20\",\"prefix_length\":24} x",
        "",
    };
    // clang-format off
    static const char *const answers[] = {
        "{\"state\":\"pending\"}",
        "{\"state\":\"pending\"}",
        "{\"code\":\"invalid_field\",\"fields\":[{\"name\":\"address\",\"code\":\"not_unicast\"},{\"name\":\"prefix_length\",\"code\":\"prefix_not_usable\"}]}",
        "{\"code\":\"bad_request\"}",
        "{\"code\":\"bad_request\"}",
        "{\"code\":\"bad_request\"}",
    };
    // clang-format on
    const session_jar_t *jar;

    web_net_setup();
    jar = web_net_jar("both");
    for (size_t at = 0; at < sizeof(pieces) / sizeof(pieces[0]); ++at) {
        piece = pieces[at];
        for (size_t index = 0; index < sizeof(bodies) / sizeof(bodies[0]); ++index) {
            const fake_reply_t *reply;
            unsigned before = reads;
            size_t length = strlen(bodies[index]);

            reply = web_net_put(jar, bodies[index]);
            if (strcmp(reply->body, answers[index]) != 0) {
                printf("  piece %zu, body %zu: %s\n", piece, index, reply->body);
            }
            CHECK(strcmp(reply->body, answers[index]) == 0);
            /* With a piece of one byte, the body took one call for each byte, and one that found
             * the end. The proof that the loop ran, and not one read. */
            if (piece == 1u && length > 1u) {
                CHECK(reads - before >= length);
            }
            (void)crux_network_settings_work_once();
        }
    }
}

int main(void)
{
    net_run_case("answers do not depend on the piece", answers_do_not_depend_on_the_piece);
    return net_finish("web_network_short_reads");
}
