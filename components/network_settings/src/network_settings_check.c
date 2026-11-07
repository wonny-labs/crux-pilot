/* The rules of the specification, the address text and the check key. Pure functions with no state,
 * so that the web routes, the command line and the boot all read one definition. */
#include <crux/network_settings.h>

#define PREFIX_MIN 1u
#define PREFIX_MAX 30u

/* The class of an address that a device may hold or route through: not 0/8, not loopback, and
 * below the multicast range, which also leaves out the reserved range and the all-ones address. */
static bool unicast_class(uint32_t address)
{
    uint32_t first = address >> 24;

    return first != 0u && first != 127u && first < 224u;
}

static bool prefix_usable(uint32_t prefix_length)
{
    return prefix_length >= PREFIX_MIN && prefix_length <= PREFIX_MAX;
}

static void add(crux_network_findings_t *findings, crux_network_field_t field,
                crux_network_problem_t problem)
{
    if (findings != NULL &&
        findings->count < sizeof(findings->items) / sizeof(findings->items[0])) {
        findings->items[findings->count].field = field;
        findings->items[findings->count].problem = problem;
        ++findings->count;
    }
}

bool crux_network_settings_validate(const crux_network_settings_t *settings,
                                    crux_network_findings_t *findings)
{
    bool usable;
    uint32_t host_mask = 0u;
    bool valid = true;

    if (findings != NULL) {
        findings->count = 0u;
    }
    if (settings == NULL) {
        return false;
    }
    usable = prefix_usable(settings->prefix_length);
    if (usable) {
        host_mask = UINT32_MAX >> settings->prefix_length;
    }

    /* The subnet's own addresses are for the network and its broadcast, and need a prefix. */
    if (!unicast_class(settings->address) ||
        (usable &&
         ((settings->address & host_mask) == 0u || (settings->address & host_mask) == host_mask))) {
        add(findings, CRUX_NETWORK_FIELD_ADDRESS, CRUX_NETWORK_PROBLEM_NOT_UNICAST);
        valid = false;
    }
    if (!usable) {
        add(findings, CRUX_NETWORK_FIELD_PREFIX_LENGTH, CRUX_NETWORK_PROBLEM_PREFIX_NOT_USABLE);
        valid = false;
    }
    if (settings->has_gateway) {
        uint32_t gateway = settings->gateway;
        bool bad = gateway == 0u || gateway == settings->address || !unicast_class(gateway);

        /* 0.0.0.0 is outside every usable subnet but 0.0.0.0/1, and no gateway is said by leaving
         * it out, so it is refused here whatever the prefix length. */
        if (!bad && usable) {
            bad = (gateway & ~host_mask) != (settings->address & ~host_mask) ||
                  (gateway & host_mask) == 0u || (gateway & host_mask) == host_mask;
        }
        if (bad) {
            add(findings, CRUX_NETWORK_FIELD_GATEWAY, CRUX_NETWORK_PROBLEM_GATEWAY_OUTSIDE_SUBNET);
            valid = false;
        }
    }
    return valid;
}

bool crux_network_settings_parse_ipv4(const char *text, size_t length, uint32_t *value)
{
    uint32_t result = 0u;
    size_t position = 0u;

    if (text == NULL || value == NULL) {
        return false;
    }
    for (unsigned octet = 0; octet < 4u; ++octet) {
        size_t digits = 0u;
        uint32_t number = 0u;

        while (position < length && text[position] >= '0' && text[position] <= '9') {
            number = number * 10u + (uint32_t)(text[position] - '0');
            ++position;
            ++digits;
            if (digits > 3u || number > 255u) {
                return false;
            }
        }
        /* No leading zero: "01" is a different spelling, and some parsers read it as octal. */
        if (digits == 0u || (digits > 1u && text[position - digits] == '0')) {
            return false;
        }
        result = (result << 8) | number;
        if (octet < 3u) {
            if (position >= length || text[position] != '.') {
                return false;
            }
            ++position;
        }
    }
    if (position != length) {
        return false;
    }
    *value = result;
    return true;
}

void crux_network_settings_format_ipv4(uint32_t address, char *text)
{
    size_t used = 0u;

    for (unsigned octet = 0; octet < 4u; ++octet) {
        uint32_t number = (address >> (24u - 8u * octet)) & 0xFFu;

        if (number >= 100u) {
            text[used++] = (char)('0' + number / 100u);
        }
        if (number >= 10u) {
            text[used++] = (char)('0' + (number / 10u) % 10u);
        }
        text[used++] = (char)('0' + number % 10u);
        if (octet < 3u) {
            text[used++] = '.';
        }
    }
    text[used] = '\0';
}

static uint32_t mix(uint32_t hash, uint32_t value)
{
    for (unsigned shift = 0; shift < 32u; shift += 8u) {
        hash ^= (value >> shift) & 0xFFu;
        hash *= 16777619u;
    }
    return hash;
}

uint32_t crux_network_settings_check_key(const crux_network_settings_t *settings)
{
    /* FNV-1a over a tag and the three members, then the 32-bit finaliser of MurmurHash3, so that a
     * change of one bit anywhere changes about half of the key. The gateway counts as zero when
     * there is none, which is how a set is stored. */
    static const char tag[] = "crux.net.v1";
    uint32_t hash = 2166136261u;
    uint32_t gateway = settings->has_gateway ? settings->gateway : 0u;

    for (size_t index = 0; index < sizeof(tag) - 1u; ++index) {
        hash ^= (uint8_t)tag[index];
        hash *= 16777619u;
    }
    hash = mix(hash, settings->address);
    hash = mix(hash, settings->prefix_length);
    hash = mix(hash, gateway);
    hash ^= hash >> 16;
    hash *= 0x85EBCA6Bu;
    hash ^= hash >> 13;
    hash *= 0xC2B2AE35u;
    hash ^= hash >> 16;
    /* Zero is what a record that was never written reads as. */
    return hash == 0u ? 1u : hash;
}
