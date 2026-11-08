/* The PSA providers of the Access and credential ports, against known answers and their failure
 * mappings. Host and native_sim run the same cases. */
#ifndef CRUX_PSA_PORT_CASES_H
#define CRUX_PSA_PORT_CASES_H

#include <stddef.h>
#include <stdint.h>

/* Returns how many checks failed. Each failure is printed. */
unsigned psa_port_cases_run(void);

/* What the device reports from now on. A length of zero means that it has no ID. */
void psa_port_test_device_id(const uint8_t *id, size_t length);

#endif
