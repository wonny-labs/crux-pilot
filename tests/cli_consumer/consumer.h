/* What a project uses: its own commands and handlers. It sees no adapter or transport type. */
#ifndef CRUX_CLI_CONSUMER_H
#define CRUX_CLI_CONSUMER_H

#include <crux/cli.h>

#ifdef __cplusplus
extern "C" {
#endif

crux_cli_result_t cli_consumer_start(void);

#ifdef __cplusplus
}
#endif

#endif
