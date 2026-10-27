#ifndef CRUX_STATUS_H
#define CRUX_STATUS_H

/** Shared Crux result codes. Zero is success; negative values are errors.
 * File and Configuration define separate result domains with additional outcomes.
 */
typedef enum {
    CRUX_OK = 0,
    CRUX_ERR_INVALID_ARGUMENT = -1,
    CRUX_ERR_OUT_OF_RANGE = -2,
    CRUX_ERR_NOT_READY = -3,       /**< Required service or initialisation is not ready. */
    CRUX_ERR_NO_DATA = -4,         /**< Service is ready, but no valid value is available. */
    CRUX_ERR_IO = -5,
    CRUX_ERR_BUSY = -6,            /**< Resource unavailable without completing a wait. */
    CRUX_ERR_TIMEOUT = -7,         /**< Finite wait expired. */
    CRUX_ERR_NO_RESOURCES = -8,    /**< Bounded resource capacity exhausted. */
    CRUX_ERR_NOT_SUPPORTED = -9,   /**< Operation unsupported by provider or context. */
    CRUX_ERR_INVALID_STATE = -10   /**< Object exists but is in an invalid lifecycle state. */
} crux_status_t;

#endif
