/* A composition with no Configuration provider, decision C6. Links the selected no-provider port
 * in place of a fake, so a load or save reaches the real functions. */
#include <crux/configuration.h>

#include <stdio.h>
#include <string.h>

static unsigned failures;
#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression)) {                                                                       \
            printf("FAIL line %d: %s\n", __LINE__, #expression);                                   \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

int main(void)
{
    crux_config_spec_t number_spec = {
        "number", 1, {.type = CRUX_CONFIG_U32, .data.u32 = 7}, NULL, NULL};
    crux_config_param_t number = {0};
    crux_config_value_t value = {.type = CRUX_CONFIG_U32, .data.u32 = 9};
    CHECK(crux_config_init(&number, &number_spec) == CRUX_CONFIG_OK);
    CHECK(crux_config_load(&number) == CRUX_CONFIG_ERR_NOT_SUPPORTED);
    CHECK(crux_config_read(&number, &value) == CRUX_CONFIG_OK && value.data.u32 == 7);
    CHECK(crux_config_save(&number) == CRUX_CONFIG_ERR_NOT_SUPPORTED);

    crux_config_value_t text_default = {.type = CRUX_CONFIG_STRING};
    text_default.data.string.text = "crux";
    text_default.data.string.length = 4;
    crux_config_spec_t text_spec = {"name", 1, text_default, NULL, NULL};
    char storage[16];
    crux_config_param_t text = {0};
    CHECK(crux_config_init_string(&text, &text_spec, storage, sizeof(storage)) == CRUX_CONFIG_OK);
    CHECK(crux_config_load(&text) == CRUX_CONFIG_ERR_NOT_SUPPORTED);
    CHECK(crux_config_read(&text, &value) == CRUX_CONFIG_OK);
    CHECK(strcmp(value.data.string.text, "crux") == 0);
    CHECK(crux_config_save(&text) == CRUX_CONFIG_ERR_NOT_SUPPORTED);

    printf("CONFIGURATION_NO_PROVIDER_%s\n", failures ? "FAIL" : "PASS");
    return failures ? 1 : 0;
}
