/**
 * @file
 * @brief Static IPv4 settings that an authorised user saves and the next boot applies.
 *
 * Thread context only. The service sits above the Configuration service: its five parameters are in
 * the composition's table under a permission that no role holds, and it reads, updates and saves
 * them with the application calls of that service. Nothing here changes a running interface. A save
 * stores values, and the composition applies them at boot, through a private port, before any
 * management channel starts. crux_netif stays read-only.
 *
 * A mixed set must never boot. A save writes the address, prefix length and gateway first, then a
 * check key over those three, then the `use_saved` flag, each in a save of its own, so that the
 * check key and the flag are always written after the values they cover. At boot the saved values
 * apply only if every record is present and valid, the flag is true, the values pass the checks
 * of crux_network_settings_validate() and the check key matches. Anything else uses the
 * composition's defaults. Saved values apply at any later reset, a power cut included.
 *
 * Boot order of a composition: Access and the Configuration provider, crux_network_settings_boot(),
 * crux_config_service_start() on a table that holds crux_network_settings_entries(), then
 * crux_network_settings_start() and crux_network_settings_start_worker(). See
 * docs/network-settings-service.md.
 */
#ifndef CRUX_NETWORK_SETTINGS_H
#define CRUX_NETWORK_SETTINGS_H

#include <crux/access.h>
#include <crux/config_service.h>

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Permission that reading the state needs. */
#define CRUX_NETWORK_SETTINGS_PERMISSION_READ "network.read"
/** Permission that saving and recovery need. */
#define CRUX_NETWORK_SETTINGS_PERMISSION_WRITE "network.write"
/**
 * The permission of the five parameters in the Configuration service's table. No role may hold it,
 * so that no session sees them in the configuration routes or can change one without the check key.
 */
#define CRUX_NETWORK_SETTINGS_PERMISSION_PRIVATE "network.private"

/** Entries that crux_network_settings_entries() returns, in the order the table must keep. */
#define CRUX_NETWORK_SETTINGS_ENTRY_COUNT 5u

/** The five Configuration keys, for tests and status text. They are not a client's names. */
#define CRUX_NETWORK_SETTINGS_KEY_ADDRESS "net.address"
#define CRUX_NETWORK_SETTINGS_KEY_PREFIX "net.prefix"
#define CRUX_NETWORK_SETTINGS_KEY_GATEWAY "net.gateway"
#define CRUX_NETWORK_SETTINGS_KEY_CHECK "net.check"
#define CRUX_NETWORK_SETTINGS_KEY_USE_SAVED "net.use_saved"

/** Longest dotted-quad text with its terminator, as crux_network_settings_format_ipv4() writes it.
 */
#define CRUX_NETWORK_SETTINGS_IPV4_TEXT_SIZE 16u

/**
 * IPv4 settings. Addresses are host-order numbers, so 192.0.2.10 is 0xC000020A. The prefix length
 * is a number of any size, so that a request's value is checked rather than truncated.
 */
typedef struct {
    uint32_t address;
    uint32_t prefix_length;
    bool has_gateway; /**< False means no gateway, which is not the address 0.0.0.0. */
    uint32_t gateway; /**< Meaningful with has_gateway. A stored set holds 0 when there is none. */
} crux_network_settings_t;

/** The request member that a finding is about. */
typedef enum {
    CRUX_NETWORK_FIELD_ADDRESS = 1,
    CRUX_NETWORK_FIELD_PREFIX_LENGTH = 2,
    CRUX_NETWORK_FIELD_GATEWAY = 3
} crux_network_field_t;

/** Stable reasons, which are the field codes of web/api/openapi.yaml. */
typedef enum {
    CRUX_NETWORK_PROBLEM_NOT_UNICAST = 1,           /**< `not_unicast` */
    CRUX_NETWORK_PROBLEM_PREFIX_NOT_USABLE = 2,     /**< `prefix_not_usable` */
    CRUX_NETWORK_PROBLEM_GATEWAY_OUTSIDE_SUBNET = 3 /**< `gateway_outside_subnet` */
} crux_network_problem_t;

typedef struct {
    crux_network_field_t field;
    crux_network_problem_t problem;
} crux_network_finding_t;

/** Every finding of one validation, in the order address, prefix length, gateway. */
typedef struct {
    size_t count;
    crux_network_finding_t items[3];
} crux_network_findings_t;

/** Results; zero is success. */
typedef enum {
    CRUX_NETWORK_OK = 0,
    CRUX_NETWORK_ERR_INVALID_ARGUMENT = -1,
    CRUX_NETWORK_ERR_NOT_READY = -2,       /**< Not started, or a lock could not be created. */
    CRUX_NETWORK_ERR_SESSION_INVALID = -3, /**< Never valid, ended or expired. */
    CRUX_NETWORK_ERR_DENIED = -4,          /**< The session lacks the permission. */
    CRUX_NETWORK_ERR_REJECTED = -5,        /**< The values fail a check. See the findings. */
    CRUX_NETWORK_ERR_BUSY = -6,            /**< A save or a recovery is queued or running. */
    CRUX_NETWORK_ERR_NOT_SUPPORTED = -7,   /**< The composition has no Configuration provider. */
    CRUX_NETWORK_ERR_STORE = -8            /**< A recovery could not save its flag. */
} crux_network_result_t;

/** Where the running values came from. */
typedef enum {
    CRUX_NETWORK_SOURCE_DEFAULT = 0,
    CRUX_NETWORK_SOURCE_SAVED = 1
} crux_network_source_t;

/** Why the boot used its defaults, or CRUX_NETWORK_BOOT_SAVED when it did not. */
typedef enum {
    CRUX_NETWORK_BOOT_SAVED = 0,         /**< The saved values passed every check and applied. */
    CRUX_NETWORK_BOOT_NOT_STORED = 1,    /**< No record of the five exists: a device never saved. */
    CRUX_NETWORK_BOOT_NOT_SUPPORTED = 2, /**< No Configuration provider. */
    CRUX_NETWORK_BOOT_STORE_ERROR = 3,   /**< A record could not be read. */
    CRUX_NETWORK_BOOT_RECORD_INVALID = 4, /**< A record failed its own checks. */
    CRUX_NETWORK_BOOT_DISABLED = 5,   /**< The `use_saved` flag is false: the recovery command. */
    CRUX_NETWORK_BOOT_INCOMPLETE = 6, /**< Some records exist and others do not: a mixed set. */
    CRUX_NETWORK_BOOT_VALUES_INVALID = 7, /**< The values fail crux_network_settings_validate(). */
    CRUX_NETWORK_BOOT_CHECK_MISMATCH = 8, /**< The check key is not that of the values. */
    CRUX_NETWORK_BOOT_APPLY_FAILED = 9 /**< The port refused the saved values, so defaults ran. */
} crux_network_boot_reason_t;

/** What the composition gives the boot. */
typedef struct {
    /** Used whenever the saved values are not. Must pass crux_network_settings_validate(). */
    crux_network_settings_t defaults;
} crux_network_settings_config_t;

/** What the boot decided and did. */
typedef struct {
    crux_network_source_t source;
    crux_network_boot_reason_t reason;
    bool applied;                   /**< False when the port refused the defaults too. */
    bool persistence;               /**< False when there is no Configuration provider. */
    crux_network_settings_t values; /**< What the port was given last. */
} crux_network_boot_report_t;

/** The state of a save, as a client sees it. */
typedef enum {
    CRUX_NETWORK_SAVE_NONE = 0,
    CRUX_NETWORK_SAVE_PENDING = 1, /**< Queued or running. */
    CRUX_NETWORK_SAVE_FAILED = 2   /**< The last save failed, until the next accepted one. */
} crux_network_save_state_t;

/** What a failed save was, for an operator. It never reaches a client. */
typedef enum {
    CRUX_NETWORK_FAILURE_NONE = 0,
    CRUX_NETWORK_FAILURE_NOT_SUPPORTED = 1,
    CRUX_NETWORK_FAILURE_STORAGE = 2,
    CRUX_NETWORK_FAILURE_BUSY = 3, /**< The Configuration service stayed busy for the whole wait. */
    CRUX_NETWORK_FAILURE_NOT_READY = 4
} crux_network_failure_t;

/** What GET shows. */
typedef struct {
    bool has_running;                /**< False while the interface has no IPv4 address. */
    crux_network_settings_t running; /**< Prefix length and gateway are the values applied. */
    crux_network_source_t source;
    bool has_saved; /**< True when the next boot would apply saved values. */
    crux_network_settings_t saved;
    bool reboot_required; /**< The next boot would apply values other than running. */
    crux_network_save_state_t save;
    crux_network_failure_t failure;
} crux_network_state_t;

/**
 * Decide whether settings are usable, with every finding, by the rules of the specification:
 *
 * - The address is of unicast class: first octet not 0 or 127, and below 224.
 * - The prefix length is 1 to 30.
 * - With a usable prefix length, the address is not the subnet's network or broadcast address.
 * - A gateway is of unicast class, is not 0.0.0.0 and is not the address. With a usable prefix
 *   length it is inside the address's subnet and is not the subnet's network or broadcast address.
 *
 * Every gateway finding is `gateway_outside_subnet`, the specification's one gateway code. With no
 * usable prefix length there is no subnet, so only the class and equality rules apply.
 *
 * @param settings Settings, with the gateway ignored unless has_gateway.
 * @param[out] findings Receives every finding, or none. May be NULL.
 * @return True if there are no findings.
 */
bool crux_network_settings_validate(const crux_network_settings_t *settings,
                                    crux_network_findings_t *findings);

/**
 * Read a dotted quad: four decimal octets of 0 to 255 with no leading zero, no sign, no space and
 * nothing before or after. Reads exactly length bytes.
 *
 * @param[out] value Host-order address. Unchanged on failure.
 * @return True if text is an address.
 */
bool crux_network_settings_parse_ipv4(const char *text, size_t length, uint32_t *value);

/**
 * Write an address as a dotted quad and a terminator.
 *
 * @param[out] text At least CRUX_NETWORK_SETTINGS_IPV4_TEXT_SIZE bytes.
 */
void crux_network_settings_format_ipv4(uint32_t address, char *text);

/**
 * The check key over a stored set: a 32-bit mix of a fixed tag and the three values, with a
 * gateway of 0 when there is none. It detects a set made of parts of two sets, which is its whole
 * purpose, and it is not a defence against anyone who can write the store.
 */
uint32_t crux_network_settings_check_key(const crux_network_settings_t *settings);

/**
 * The five Configuration entries, in the order the table must keep: address, prefix length,
 * gateway, check key, then `use_saved`. Copy them into the composition's table. They point at
 * storage that this component owns, and they are valid for the program's life.
 *
 * @param[out] count CRUX_NETWORK_SETTINGS_ENTRY_COUNT. May be NULL.
 */
const crux_config_service_entry_t *crux_network_settings_entries(size_t *count);

/**
 * Initialise and load the five parameters, decide, and apply through the private port.
 *
 * Before crux_config_service_start() and crux_network_settings_start(), on one thread, because the
 * service owns the parameters afterwards. It loads each parameter itself, and a load that fails
 * only means that the defaults apply. The saved values apply only if every check of the file
 * comment holds. If the port refuses them, the defaults are tried, and the report says so. Calling
 * it again resets everything, which only a Host test does.
 *
 * @param config The composition's defaults.
 * @param[out] report What was decided and applied. May be NULL.
 * @retval CRUX_NETWORK_OK Decided. Check report->applied to see whether anything applied.
 * @retval CRUX_NETWORK_ERR_INVALID_ARGUMENT The config is missing, or its defaults fail the checks.
 */
crux_network_result_t crux_network_settings_boot(const crux_network_settings_config_t *config,
                                                 crux_network_boot_report_t *report);

/**
 * Create the locks and check that the Configuration service holds the five parameters.
 *
 * @retval CRUX_NETWORK_OK Ready.
 * @retval CRUX_NETWORK_ERR_NOT_READY crux_network_settings_boot() has not run, a lock could not be
 * created, or the table lacks a parameter or holds it with another type.
 */
crux_network_result_t crux_network_settings_start(void);

/**
 * Start the one worker thread that saves, at low priority. A composition with no thread calls
 * crux_network_settings_work_once() itself.
 *
 * @retval CRUX_NETWORK_OK Started, or already running.
 * @retval CRUX_NETWORK_ERR_NOT_READY Not started, or the thread could not be created.
 */
crux_network_result_t crux_network_settings_start_worker(void);

/**
 * The state for GET: running values, saved values and their source.
 *
 * Needs `network.read`, checked with crux_access_check(), which records a denial. Polls its lock
 * for a short time and never waits on storage.
 *
 * @param[out] state Unchanged on failure.
 * @retval CRUX_NETWORK_OK Copied.
 * @retval CRUX_NETWORK_ERR_INVALID_ARGUMENT The state is missing.
 * @retval CRUX_NETWORK_ERR_NOT_READY Not started.
 * @retval CRUX_NETWORK_ERR_BUSY The lock did not come in time. Retry later.
 * @retval CRUX_NETWORK_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_NETWORK_ERR_DENIED The session lacks `network.read`.
 */
crux_network_result_t crux_network_settings_get(crux_access_session_t session,
                                                crux_network_state_t *state);

/**
 * Validate settings and queue their save for the worker.
 *
 * Needs `network.write`. A refused request changes nothing and leaves no event except Access's
 * denial. An accepted one ends in exactly one event, `network.changed` when the whole set is
 * saved and `network.save_failed` when it is not. The result of the save is read through
 * crux_network_settings_get(). Never waits on storage, so it may run on the server thread.
 *
 * @param settings The new settings.
 * @param[out] findings Receives the findings of a refusal. May be NULL.
 * @retval CRUX_NETWORK_OK Queued.
 * @retval CRUX_NETWORK_ERR_INVALID_ARGUMENT The settings are missing.
 * @retval CRUX_NETWORK_ERR_NOT_READY Not started.
 * @retval CRUX_NETWORK_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_NETWORK_ERR_DENIED The session lacks `network.write`.
 * @retval CRUX_NETWORK_ERR_REJECTED A value fails a check.
 * @retval CRUX_NETWORK_ERR_NOT_SUPPORTED There is no Configuration provider, so nothing could be
 * saved.
 * @retval CRUX_NETWORK_ERR_BUSY A save is queued or running, a recovery is, or the lock did not
 * come in time.
 */
crux_network_result_t crux_network_settings_request(crux_access_session_t session,
                                                    const crux_network_settings_t *settings,
                                                    crux_network_findings_t *findings);

/**
 * Run the queued save, if there is one, on the calling thread. The worker loops on it, and a Host
 * test calls it in place of the worker. It may wait on storage.
 *
 * @return True if a save ran.
 */
bool crux_network_settings_work_once(void);

/**
 * Stop the next boot from using the saved values, by saving the `use_saved` flag as false.
 *
 * Configuration has no clear operation, so the saved values stay stored and unused until the next
 * successful save, which sets the flag true as the last record of its set. For the serial
 * recovery command, on the console's own thread, because it waits on storage. Needs
 * `network.write`. Saving the flag writes every parameter that something marked unsaved, because
 * the Configuration service saves marks and not names.
 *
 * @retval CRUX_NETWORK_OK Saved as false, and `network.saved_ignored` recorded.
 * @retval CRUX_NETWORK_ERR_NOT_READY Not started.
 * @retval CRUX_NETWORK_ERR_SESSION_INVALID Never valid, ended or expired.
 * @retval CRUX_NETWORK_ERR_DENIED The session lacks `network.write`.
 * @retval CRUX_NETWORK_ERR_NOT_SUPPORTED There is no Configuration provider.
 * @retval CRUX_NETWORK_ERR_BUSY A save is queued or running, or another recovery is.
 * @retval CRUX_NETWORK_ERR_STORE The flag could not be saved, and `network.save_failed` was
 * recorded. The flag may or may not be stored, so run the command again.
 */
crux_network_result_t crux_network_settings_recover(crux_access_session_t session);

/**
 * Whether a save or a recovery is queued or running, for a service that must not cut one short,
 * such as the reboot service's wait. Takes no session and records nothing. It polls its lock for a
 * short time and never waits on storage, and a lock that does not come in time reads as pending,
 * the safe answer.
 *
 * @return True while a save or a recovery is in flight. False before crux_network_settings_start().
 */
bool crux_network_settings_save_pending(void);

/** What the boot decided, for the composition to log. Zeroed before crux_network_settings_boot().
 */
void crux_network_settings_boot_report(crux_network_boot_report_t *report);

#ifdef __cplusplus
}
#endif

#endif
