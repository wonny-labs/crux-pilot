/* The handler of the packaged web assets. The data, headers and routes are generated, see
 * web_assets_internal.h. */
#include "web_assets_internal.h"

crux_web_result_t crux_web_assets_serve(crux_web_request_t *request, crux_access_session_t session,
                                        void *context)
{
    const crux_web_asset_t *asset = (const crux_web_asset_t *)context;

    /* The route is public: the bundle is what a browser loads before login. */
    (void)session;
    if (asset == NULL) {
        return CRUX_WEB_ERR_INVALID_ARGUMENT;
    }
    return crux_web_respond_static(request, 200, asset->headers, asset->header_count, asset->data,
                                   asset->length);
}
