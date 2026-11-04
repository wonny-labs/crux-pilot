/* Rule L5 of the command line contract, at compilation, for any build of this component. It repeats
 * cmake/CruxManagementShell.cmake, in case a composition adds these sources some other way. It uses
 * the preprocessor alone, so tests/build can check it on the Host with no Zephyr headers. */

#if (defined(CONFIG_SHELL) || defined(CONFIG_SHELL_BACKEND_SERIAL)) &&                             \
    !defined(CONFIG_CRUX_DEVELOPMENT_IMAGE)
#error "An image with Crux management must not enable CONFIG_SHELL unless it is marked development"
#endif

#if defined(CONFIG_SHELL_BACKEND_TELNET) || defined(CONFIG_SHELL_BACKEND_WEBSOCKET) ||             \
    defined(CONFIG_SHELL_BACKEND_MQTT) || defined(CONFIG_MCUMGR_GRP_SHELL)
#error "The Zephyr shell never reaches the network"
#endif

#if defined(CONFIG_SHELL_BACKEND_RTT) || defined(CONFIG_SHELL_BACKEND_RPMSG) ||                    \
    defined(CONFIG_SHELL_BACKEND_DUMMY) || defined(CONFIG_SHELL_BACKEND_ADSP_MEMORY_WINDOW)
#error "An image with Crux management may use only the serial shell backend"
#endif

/* ISO C needs a declaration in every translation unit. */
typedef int crux_cli_shell_rule_checked_t;
