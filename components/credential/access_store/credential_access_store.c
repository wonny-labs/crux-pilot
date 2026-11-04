/*
 * The store port of crux_access, on the credential store. Accounts are one record. EMPTY means only
 * that this record is absent from a readable store, so device keys generated first do not close
 * provisioning. Anything that does not prove absence is UNUSABLE, so it can never open
 * provisioning.
 */
#include "access_port.h"
#include "credential_port.h"

_Static_assert(CRUX_ACCESS_RECORD_SIZE_MAX <= CRUX_CREDENTIAL_ACCOUNTS_BYTES_MAX,
               "The credential store must hold the largest Access record");

crux_access_store_state_t crux_access_port_store_load(uint8_t *record, size_t capacity,
                                                      size_t *length)
{
    size_t read_length = 0;
    switch (crux_credential_read(CRUX_CREDENTIAL_RECORD_ACCOUNTS, record, capacity, &read_length)) {
    case CRUX_CREDENTIAL_OK:
        *length = read_length;
        return CRUX_ACCESS_STORE_VALID;
    case CRUX_CREDENTIAL_ERR_NOT_FOUND:
        return CRUX_ACCESS_STORE_EMPTY;
    default:
        return CRUX_ACCESS_STORE_UNUSABLE;
    }
}

bool crux_access_port_store_save(const uint8_t *record, size_t length)
{
    return crux_credential_write(CRUX_CREDENTIAL_RECORD_ACCOUNTS, record, length) ==
           CRUX_CREDENTIAL_OK;
}
