#include "message.h"

extern struct event_loop g_event_loop;
extern struct display_manager g_display_manager;
extern struct window_manager g_window_manager;
extern bool g_verbose;

#define DOMAIN_CONFIG  "config"

/* --------------------------------DOMAIN CONFIG-------------------------------- */
#define COMMAND_CONFIG_DEBUG_OUTPUT          "debug_output"
/* ----------------------------------------------------------------------------- */

/* --------------------------------COMMON ARGUMENTS----------------------------- */
#define ARGUMENT_COMMON_VAL_ON     "on"
#define ARGUMENT_COMMON_VAL_OFF    "off"
#define ARGUMENT_COMMON_SEL_PREV   "prev"
#define ARGUMENT_COMMON_SEL_NEXT   "next"
#define ARGUMENT_COMMON_SEL_FIRST  "first"
#define ARGUMENT_COMMON_SEL_LAST   "last"
#define ARGUMENT_COMMON_SEL_RECENT "recent"
/* ----------------------------------------------------------------------------- */

static bool token_equals(struct token token, char *match)
{
    char *at = match;
    for (int i = 0; i < token.length; ++i, ++at) {
        if ((*at == 0) || (token.text[i] != *at)) {
            return false;
        }
    }
    return *at == 0;
}

static bool token_is_valid(struct token token)
{
    return token.text && token.length > 0;
}

static char *token_to_string(struct token token)
{
    char *result = malloc(token.length + 1);
    if (!result) return NULL;

    memcpy(result, token.text, token.length);
    result[token.length] = '\0';
    return result;
}

static uint32_t token_to_uint32t(struct token token)
{
    uint32_t result = 0;
    char buffer[token.length + 1];
    memcpy(buffer, token.text, token.length);
    buffer[token.length] = '\0';
    sscanf(buffer, "%x", &result);
    return result;
}

static bool token_to_int(struct token token, int *value)
{
    int result = 0;
    char buffer[token.length + 1];
    memcpy(buffer, token.text, token.length);
    buffer[token.length] = '\0';
    bool success = sscanf(buffer, "%d", &result) == 1;
    *value = result;
    return success;
}

static float token_to_float(struct token token)
{
    float result = 0.0f;
    char buffer[token.length + 1];
    memcpy(buffer, token.text, token.length);
    buffer[token.length] = '\0';
    sscanf(buffer, "%f", &result);
    return result;
}

static struct token get_token(char **message)
{
    struct token token;

    token.text = *message;
    while (**message) {
        ++(*message);
    }
    token.length = *message - token.text;

    if ((*message)[0] == '\0' && (*message)[1] != '\0') {
        ++(*message);
    } else {
        // NOTE(koekeishiya): don't go past the null-terminator
    }

    return token;
}

static void daemon_fail(FILE *rsp, char *fmt, ...)
{
    if (!rsp) return;

    fprintf(rsp, FAILURE_MESSAGE);

    va_list ap;
    va_start(ap, fmt);
    vfprintf(rsp, fmt, ap);
    va_end(ap);
}

static void handle_domain_config(FILE *rsp, struct token domain, char *message)
{
    struct token command = get_token(&message);
    if (token_equals(command, COMMAND_CONFIG_DEBUG_OUTPUT)) {
        struct token value = get_token(&message);
        if (!token_is_valid(value)) {
            fprintf(rsp, "%s\n", bool_str[g_verbose]);
        } else if (token_equals(value, ARGUMENT_COMMON_VAL_OFF)) {
            g_verbose = false;
        } else if (token_equals(value, ARGUMENT_COMMON_VAL_ON)) {
            g_verbose = true;
        } else {
            daemon_fail(rsp, "unknown value '%.*s' given to command '%.*s' for domain '%.*s'\n", value.length, value.text, command.length, command.text, domain.length, domain.text);
        }
    } else {
        daemon_fail(rsp, "unknown command '%.*s' for domain '%.*s'\n", command.length, command.text, domain.length, domain.text);
    }
}

void handle_message(FILE *rsp, char *message)
{
    struct token domain = get_token(&message);
    if (token_equals(domain, DOMAIN_CONFIG)) {
        handle_domain_config(rsp, domain, message);
    } else {
        daemon_fail(rsp, "unknown domain '%.*s'\n", domain.length, domain.text);
    }
}

static SOCKET_DAEMON_HANDLER(message_handler)
{
    struct event *event = event_create_p1(&g_event_loop, DAEMON_MESSAGE, message, sockfd);
    event_loop_post(&g_event_loop, event);
}
