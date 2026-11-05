/* What the network settings suites share: the real Access core on its fake ports, the real
 * Configuration service on the fake Configuration provider, a fake apply port and a recording event
 * port. Each case runs in a forked child, so that every case starts from nothing. */
#ifndef NETWORK_SETTINGS_TEST_H
#define NETWORK_SETTINGS_TEST_H

#include "access_test.h"
#include "fault_provider.h"
#include "network_settings_internal.h"
#include "network_settings_port.h"

#include <crux/config_service.h>
#include <crux/network_settings.h>

#include <stdlib.h>

void net_run_case(const char *name, void (*body)(void));
int net_finish(const char *suite);

/* The sessions. Each account has the password ACCESS_TEST_PASSWORD.
 *   admin     access.manage, network.read, network.write, config.save, cfg.read, cfg.write
 *   reader    network.read
 *   writer    network.write
 *   both      network.read and network.write
 *   saver     config.save, cfg.read and cfg.write, which are the configuration routes' rights
 *   stranger  other.thing
 *   forced    both, under a forced password change
 */
extern crux_access_session_t s_admin, s_reader, s_writer, s_both, s_saver, s_stranger, s_forced;
extern const crux_access_session_t s_never_valid;

/* The composition's defaults: 192.0.2.10/24 with no gateway. */
extern const crux_network_settings_t net_defaults;
/* Two complete sets that differ in every member, and a third that shares the address with the
 * second. */
extern const crux_network_settings_t net_set_a, net_set_b;

/* The table that the case uses: one ordinary parameter, the five of the service, one more. */
#define NET_TABLE_ENTRIES (CRUX_NETWORK_SETTINGS_ENTRY_COUNT + 2u)
#define NET_CFG_LIMIT "limit"
#define NET_CFG_TAIL "tail"

typedef struct {
    bool keep_store;       /* Do not empty the fake Configuration store: a reboot. */
    size_t apply_failures; /* The first applies that the fake port refuses. */
} net_options_t;

/* Boots Access, the accounts and the Configuration store, then runs crux_network_settings_boot()
 * with net_defaults. Starts no service. */
void net_boot_with(const net_options_t *options, crux_network_boot_report_t *report);
void net_boot(crux_network_boot_report_t *report);
/* The above, then the Configuration service on the table, and crux_network_settings_start(). Runs
 * no worker. */
void net_start(void);

/* What the store holds, written the way the Configuration mechanism writes it. */
void net_store_u32(const char *key, uint32_t value);
void net_store_bool(const char *key, bool value);
/* The five records of a complete set, with the check key of the set unless it is overridden. */
void net_store_set(const crux_network_settings_t *set, bool use_saved);

/* A reboot's decision on what the store now holds, through the code that boot runs. */
crux_network_boot_reason_t net_reboot_decision(crux_network_settings_t *values);

/* A table of the case's parameters, for a suite that starts the service itself. */
const crux_config_service_entry_t *net_table(void);

/* The fake apply port. Every case starts with it empty and applying successfully. */
void net_port_reset(void);
size_t net_port_apply_count(void);
const crux_network_settings_t *net_port_applied(size_t index);
/* The next n applies fail. */
void net_port_fail_applies(size_t count);
/* Overrides what the interface reports. */
void net_port_set_running(bool has_running, const crux_network_settings_t *settings);

/* The recording event port. */
typedef struct {
    crux_network_settings_event_t event;
    crux_access_session_t session;
} net_event_t;
void net_events_clear(void);
size_t net_events_count(void);
const net_event_t *net_events_get(size_t index);
size_t net_events_of(crux_network_settings_event_t event);

/* Denials that Access recorded. */
size_t net_denials(void);

bool net_same(const crux_network_settings_t *a, const crux_network_settings_t *b);
crux_network_settings_t net_stored_form(const crux_network_settings_t *settings);

/* Cut the power after `writes` more successful writes: the next write fails, and so does every one
 * after it, and none changes anything, except that the first of them stores its record when
 * last_write_lands is true, as a write does whose acknowledgement is lost. */
void net_power_cut_after(size_t writes, bool last_write_lands);

/* Starts the service on a store that already holds what the case wrote with net_store_*. */
void net_start_on_store(void);

#endif
