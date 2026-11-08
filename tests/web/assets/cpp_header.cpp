/* The asset header, which the generated source includes, must compile for C++ too. */
#include "web_assets_internal.h"

static_assert(sizeof(crux_web_asset_t) > 0, "the asset type is complete");
