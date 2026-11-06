# A page and a route of a project's own

The small example project of [Issue #220](https://github.com/wonny-labs/crux-pilot/issues/220). A product adds its
own page and its own API route beside the management routes, with Crux's public contracts and no native header. It is
an example, not a template that Crux maintains, and it holds no product-ready interface.

| Part | File | Uses |
| --- | --- | --- |
| The page | `site/index.html`, `site/greeting.js`, `site/page.css` | Any tool, or none. It loads under the strict content policy: no inline script or style, nothing from another origin. |
| The route | `src/greeting_route.c` | `CRUX_WEB_ROUTE` from `<crux/web.h>`, and `crux_web_respond`. No Zephyr, board or vendor header. |
| The review | `reviewed-routes.txt` | One line for the route, which a person reviews, and one for the packaged folder |

The route answers `GET /api/v1/project/greeting` with JSON, for a signed-in session only: its access class is
`CRUX_WEB_SESSION`, which the composition's session lookup checks before the handler runs. A route that needs a
permission names it in place of `CRUX_WEB_SESSION`.

## What a composition writes

The folder and the route source are named in the composition's CMake, beside the component route sources it already
names. One `crux_web_routes()` call takes every route source of the image.

```cmake
crux_web_assets(app FOLDER ${CMAKE_CURRENT_LIST_DIR}/site STYLE strict)
crux_web_routes(app
    SOURCES ${CMAKE_CURRENT_LIST_DIR}/src/greeting_route.c ${CRUX_WEB_SESSION_ROUTES} ${CRUX_WEB_MANAGEMENT_ROUTES}
    ZEPHYR_RESOURCES REVIEWED reviewed-routes.txt)
```

The composition's own reviewed list then holds the management routes' lines too, as
`tests/native/management_services/reviewed-routes.txt` does.

## Evidence

| State | Here |
| --- | --- |
| Mock | Not applicable: the mock serves the management API only |
| Host | `Web.project_sample`, on the real web core and the fake transport: the folder packaged by the real packager under the strict setting, the route behind a session lookup, and no fallback. `tests/build/test_project_sample.py` checks that the sources include no native header and the page has no inline content. |
| `native_sim` | Not run: the image check of the services image allows only the components' routes, so this route would need its own image |
| Physical | Not run |

The design note is in [the end-to-end design](../../docs/e2e-native-220.md#the-small-example-project).
