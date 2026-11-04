/*
 * The boundary the proposal asks for: the real file service, given the store's real roots as its
 * reserved list, refuses an area on either slot. So a client's path can never reach a slot, and
 * removing files through SFTP or the web interface cannot make the store read as EMPTY.
 */
#include "credential_test.h"
#include "file_service_port.h"

// NOLINTBEGIN(bugprone-easily-swappable-parameters): the private port fixes this signature
void crux_file_service_port_event(crux_file_service_event_t event, crux_access_session_t session,
                                  const char *area, const char *name, const char *new_name,
                                  bool storage_limit)
// NOLINTEND(bugprone-easily-swappable-parameters)
{
    (void)event;
    (void)session;
    (void)area;
    (void)name;
    (void)new_name;
    (void)storage_limit;
}

static crux_file_root_t reserved[2];
static crux_file_area_t area = {
    "data", 0, "data.read", "data.write", 1000, 2000, 5, NULL, CRUX_FILE_SERVICE_MANAGED};
static const crux_file_service_config_t config = {&area, 1, reserved, 2, false};

int main(void)
{
    test_fresh();
    CHECK(test_boot() == CRUX_CREDENTIAL_OK);
    CHECK(crux_credential_roots(reserved) == CRUX_CREDENTIAL_OK);
    CHECK(reserved[0] == TEST_ROOT_A && reserved[1] == TEST_ROOT_B);

    area.root = reserved[0];
    CHECK(crux_file_service_start(&config) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);
    area.root = reserved[1];
    CHECK(crux_file_service_start(&config) == CRUX_FILE_SERVICE_ERR_INVALID_ARGUMENT);

    /* Any other root is accepted, which shows that it was the reserved list that refused them. */
    area.root = TEST_ROOT_UNBOUND;
    CHECK(crux_file_service_start(&config) == CRUX_FILE_SERVICE_OK);
    return credential_test_finish("the file service on the store's roots");
}
