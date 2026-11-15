#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cJSON.h"
#include "forebay/routing.h"
#include "measure.h"

int32_t forebay_library_routing_arrival(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t forebay_library_routing_step(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
void forebay_library_release(uint8_t *buffer);

#define PORT 8080

static const forebay_header request_headers[] = {
    {"host", "127.0.0.1:8080"},          {"content-type", "application/json"}, {"accept", "application/json"},
    {"user-agent", "spike-client/1.0"},  {"anthropic-version", "2023-06-01"},  {"x-request-id", "4f1c2b7e-0d2a"},
};
static const forebay_headers arrival_headers = {request_headers, sizeof(request_headers) / sizeof(request_headers[0])};

static const forebay_assignment large_chain[] = {
    {"alpha", "alpha-large", "Alpha Large", 0},
    {"beta", "beta-large", "Beta Large", 0},
};
static const forebay_assignment small_chain[] = {
    {"alpha", "alpha-small", "Alpha Small", 0},
    {"beta", "beta-small", "Beta Small", 0},
};
static const forebay_model_slot slots[] = {
    {"large", large_chain, 2},
    {"small", small_chain, 2},
};
static const char *const providers[] = {"alpha", "beta"};
static const forebay_profile profile = {sizeof(forebay_profile), NULL, {NULL, 0}, {NULL, 0}, NULL, NULL, NULL};

static const char *const thrown_message = "refused";

typedef struct json_text {
    char *bytes;
    size_t length;
} json_text;

static int failures = 0;

static void fail(const char *what) {
    fprintf(stderr, "FAIL %s\n", what);
    failures++;
}

static forebay_step_argument start_argument(void) {
    forebay_step_argument argument;
    memset(&argument, 0, sizeof argument);
    argument.size = sizeof argument;
    argument.providers.items = providers;
    argument.providers.count = 2;
    argument.profile = &profile;
    argument.map.slots = slots;
    argument.map.count = 2;
    argument.model = "acme-large-1";
    argument.login_command = "host-cli action session";
    argument.settings_file = "/home/settings.json";
    return argument;
}

static forebay_report report_of(uint32_t kind, const char *message) {
    forebay_report report;
    memset(&report, 0, sizeof report);
    report.size = sizeof report;
    report.kind = kind;
    report.message = message;
    return report;
}

static forebay_step_argument report_argument(const forebay_walk *walk, const forebay_report *report) {
    forebay_step_argument argument;
    memset(&argument, 0, sizeof argument);
    argument.size = sizeof argument;
    argument.providers.items = providers;
    argument.providers.count = 2;
    argument.walk = walk;
    argument.report = report;
    return argument;
}

static cJSON *start_tree(void) {
    cJSON *tree = cJSON_CreateObject();
    cJSON *names = cJSON_CreateStringArray((const char *const *)providers, 2);
    cJSON_AddItemToObject(tree, "providers", names);
    cJSON *profile_json = cJSON_AddObjectToObject(tree, "profile");
    cJSON_AddNullToObject(profile_json, "tierSourceProvider");
    cJSON_AddArrayToObject(profile_json, "tierOrder");
    cJSON_AddArrayToObject(profile_json, "tierFallback");
    cJSON_AddNullToObject(profile_json, "tierRegex");
    cJSON_AddNullToObject(profile_json, "nativeModelPattern");
    cJSON *map = cJSON_AddObjectToObject(tree, "map");
    for (size_t slot = 0; slot < 2; slot++) {
        cJSON *chain = cJSON_AddArrayToObject(map, slots[slot].slot);
        for (size_t entry = 0; entry < slots[slot].count; entry++) {
            cJSON *item = cJSON_CreateObject();
            cJSON_AddStringToObject(item, "provider", slots[slot].chain[entry].provider);
            cJSON_AddStringToObject(item, "model", slots[slot].chain[entry].model);
            cJSON_AddStringToObject(item, "name", slots[slot].chain[entry].name);
            cJSON_AddFalseToObject(item, "derived");
            cJSON_AddItemToArray(chain, item);
        }
    }
    cJSON_AddObjectToObject(tree, "models");
    cJSON *request = cJSON_AddObjectToObject(tree, "request");
    cJSON_AddStringToObject(request, "model", "acme-large-1");
    cJSON_AddFalseToObject(tree, "refreshed");
    cJSON *wording = cJSON_AddObjectToObject(tree, "wording");
    cJSON_AddStringToObject(wording, "loginCommand", "host-cli action session");
    cJSON_AddStringToObject(wording, "settingsFile", "/home/settings.json");
    return tree;
}

static json_text printed(const cJSON *tree) {
    json_text text;
    text.bytes = cJSON_PrintUnformatted(tree);
    text.length = strlen(text.bytes);
    return text;
}

static cJSON *json_call(int32_t (*function)(const uint8_t *, size_t, uint8_t **, size_t *), const json_text *text) {
    uint8_t *out = NULL;
    size_t out_len = 0;
    int32_t status = function((const uint8_t *)text->bytes, text->length, &out, &out_len);
    cJSON *answer = status == 0 ? cJSON_ParseWithLength((const char *)out, out_len) : NULL;
    forebay_library_release(out);
    return answer;
}

static json_text report_text(const cJSON *walk, const char *kind, const char *message) {
    cJSON *tree = cJSON_CreateObject();
    cJSON_AddItemToObject(tree, "providers", cJSON_CreateStringArray((const char *const *)providers, 2));
    cJSON_AddItemToObject(tree, "walk", cJSON_Duplicate(walk, 1));
    cJSON *report = cJSON_AddObjectToObject(tree, "report");
    cJSON_AddStringToObject(report, "kind", kind);
    if (message != NULL) {
        cJSON_AddStringToObject(report, "message", message);
    }
    json_text text = printed(tree);
    cJSON_Delete(tree);
    return text;
}

static json_text arrival_text_of(const forebay_headers *headers, const char *path, int port) {
    cJSON *tree = cJSON_CreateObject();
    cJSON_AddStringToObject(tree, "path", path);
    cJSON *object = cJSON_AddObjectToObject(tree, "headers");
    for (size_t index = 0; index < headers->count; index++) {
        cJSON_AddStringToObject(object, headers->entries[index].name, headers->entries[index].value);
    }
    cJSON_AddNumberToObject(tree, "port", port);
    json_text text = printed(tree);
    cJSON_Delete(tree);
    return text;
}

static const char *json_step_kind(const cJSON *answer) {
    const cJSON *kind = cJSON_GetObjectItemCaseSensitive(cJSON_GetObjectItemCaseSensitive(answer, "step"), "kind");
    return cJSON_IsString(kind) ? kind->valuestring : "";
}

static json_text walk_texts[3];

static void prepare_walk_texts(void) {
    cJSON *start = start_tree();
    walk_texts[0] = printed(start);
    cJSON_Delete(start);
    cJSON *first = json_call(forebay_library_routing_step, &walk_texts[0]);
    walk_texts[1] = report_text(cJSON_GetObjectItemCaseSensitive(first, "walk"), "thrown", thrown_message);
    cJSON *second = json_call(forebay_library_routing_step, &walk_texts[1]);
    walk_texts[2] = report_text(cJSON_GetObjectItemCaseSensitive(second, "walk"), "response", NULL);
    if (strcmp(json_step_kind(first), "callProvider") != 0 || strcmp(json_step_kind(second), "callProvider") != 0) {
        fail("the JSON walk does not call alpha then beta");
    }
    cJSON_Delete(first);
    cJSON_Delete(second);
}

static void check_arrival(const char *label, const char *path, const forebay_headers *headers, int port) {
    const forebay_routing_definition *routing = forebay_library_routing_definition();
    json_text text = arrival_text_of(headers, path, port);
    cJSON *json = json_call(forebay_library_routing_arrival, &text);
    forebay_arrival_argument argument = {sizeof argument, path, *headers, (uint32_t)port};
    forebay_arrival_answer answer;
    memset(&answer, 0, sizeof answer);
    answer.size = sizeof answer;
    spisor_status status = routing->arrival(routing->context, &argument, &answer, NULL);
    const char *kind = cJSON_GetObjectItemCaseSensitive(json, "kind")->valuestring;
    const char *expected = answer.kind == FOREBAY_ARRIVAL_ROUTE    ? "route"
                           : answer.kind == FOREBAY_ARRIVAL_HEALTH ? "health"
                                                                   : "refuse";
    if (status != SPISOR_OK || strcmp(kind, expected) != 0) {
        fail(label);
    }
    if (answer.kind == FOREBAY_ARRIVAL_REFUSE) {
        const cJSON *reply = cJSON_GetObjectItemCaseSensitive(json, "reply");
        if (strcmp(cJSON_GetObjectItemCaseSensitive(reply, "message")->valuestring, answer.message) != 0 ||
            cJSON_GetObjectItemCaseSensitive(reply, "status")->valuedouble != answer.status) {
            fail(label);
        }
    }
    routing->release_arrival(routing->context, &answer);
    cJSON_Delete(json);
    cJSON_free(text.bytes);
}

static void expect_struct_step(const forebay_step_answer *answer, uint32_t kind, const char *model,
                               const cJSON *json, const char *label) {
    const cJSON *step = cJSON_GetObjectItemCaseSensitive(json, "step");
    const cJSON *log = cJSON_GetObjectItemCaseSensitive(json, "log");
    if (answer->kind != kind || (model != NULL && (answer->model == NULL || strcmp(answer->model, model) != 0)) ||
        answer->log_count != (size_t)cJSON_GetArraySize(log) ||
        (model != NULL && strcmp(cJSON_GetObjectItemCaseSensitive(step, "model")->valuestring, model) != 0)) {
        fail(label);
    }
    for (size_t index = 0; index < answer->log_count; index++) {
        const cJSON *line = cJSON_GetArrayItem(log, (int)index);
        if (strcmp(cJSON_GetObjectItemCaseSensitive(line, "message")->valuestring, answer->log[index].message) != 0) {
            fail(label);
        }
    }
}

static void check_walk(void) {
    const forebay_routing_definition *routing = forebay_library_routing_definition();
    forebay_step_argument start = start_argument();
    forebay_step_answer first = {.size = sizeof first};
    forebay_step_answer second = {.size = sizeof second};
    forebay_step_answer third = {.size = sizeof third};
    cJSON *json[3];
    for (int index = 0; index < 3; index++) {
        json[index] = json_call(forebay_library_routing_step, &walk_texts[index]);
    }
    if (routing->step(routing->context, &start, &first, NULL) != SPISOR_OK || first.walk == NULL) {
        fail("the first struct step");
        return;
    }
    expect_struct_step(&first, FOREBAY_STEP_CALL_PROVIDER, "alpha-large", json[0], "the first struct step");
    forebay_report thrown = report_of(FOREBAY_REPORT_THROWN, thrown_message);
    forebay_step_argument reported = report_argument(first.walk, &thrown);
    if (routing->step(routing->context, &reported, &second, NULL) != SPISOR_OK || second.walk == NULL) {
        fail("the second struct step");
        return;
    }
    expect_struct_step(&second, FOREBAY_STEP_CALL_PROVIDER, "beta-large", json[1], "the second struct step");
    forebay_report response = report_of(FOREBAY_REPORT_RESPONSE, NULL);
    forebay_step_argument served = report_argument(second.walk, &response);
    if (routing->step(routing->context, &served, &third, NULL) != SPISOR_OK || third.walk != NULL) {
        fail("the third struct step");
        return;
    }
    expect_struct_step(&third, FOREBAY_STEP_DONE, NULL, json[2], "the third struct step");
    routing->release_walk(routing->context, first.walk);
    routing->release_walk(routing->context, second.walk);
    routing->release_step(routing->context, &first);
    routing->release_step(routing->context, &second);
    routing->release_step(routing->context, &third);
    for (int index = 0; index < 3; index++) {
        cJSON_Delete(json[index]);
    }
}

static void check_refusals(void) {
    const forebay_routing_definition *routing = forebay_library_routing_definition();
    forebay_arrival_argument argument = {sizeof argument, "/", arrival_headers, 0};
    forebay_arrival_answer answer = {.size = sizeof answer};
    spisor_refusal refusal = {.size = sizeof refusal};
    if (routing->arrival(routing->context, &argument, &answer, &refusal) != SPISOR_MALFORMED || refusal.detail == NULL) {
        fail("port 0 is malformed");
    }
    routing->release_refusal(routing->context, &refusal);
}

static void check(void) {
    static const forebay_header with_origin[] = {{"host", "127.0.0.1:8080"}, {"Origin", "https://example.com"}};
    static const forebay_header named[] = {{"Host", "LOCALHOST:8080"}};
    static const forebay_header foreign[] = {{"host", "example.com"}};
    forebay_headers origin = {with_origin, 2};
    forebay_headers by_name = {named, 1};
    forebay_headers elsewhere = {foreign, 1};
    check_arrival("route", "/v1/messages?beta=true", &arrival_headers, PORT);
    check_arrival("health", "/health?x=1", &arrival_headers, PORT);
    check_arrival("origin", "/v1/messages", &origin, PORT);
    check_arrival("name", "/health", &by_name, PORT);
    check_arrival("foreign", "/health", &elsewhere, PORT);
    check_arrival("port", "/health", &arrival_headers, 8081);
    check_walk();
    check_refusals();
}

static json_text arrival_text;

static int arrival_json(void *data) {
    (void)data;
    uint8_t *out = NULL;
    size_t out_len = 0;
    int32_t status = forebay_library_routing_arrival((const uint8_t *)arrival_text.bytes, arrival_text.length, &out, &out_len);
    forebay_library_release(out);
    return status == 0;
}

static int arrival_json_round_trip(void *data) {
    (void)data;
    json_text text = arrival_text_of(&arrival_headers, "/v1/messages?beta=true", PORT);
    cJSON *answer = json_call(forebay_library_routing_arrival, &text);
    int routed = answer != NULL && strcmp(cJSON_GetObjectItemCaseSensitive(answer, "kind")->valuestring, "route") == 0;
    cJSON_Delete(answer);
    cJSON_free(text.bytes);
    return routed;
}

static int arrival_struct(void *data) {
    (void)data;
    const forebay_routing_definition *routing = forebay_library_routing_definition();
    forebay_arrival_argument argument = {sizeof argument, "/v1/messages?beta=true", arrival_headers, PORT};
    forebay_arrival_answer answer = {.size = sizeof answer};
    spisor_status status = routing->arrival(routing->context, &argument, &answer, NULL);
    int routed = status == SPISOR_OK && answer.kind == FOREBAY_ARRIVAL_ROUTE;
    routing->release_arrival(routing->context, &answer);
    return routed;
}

static int step_json(void *data) {
    (void)data;
    int done = 1;
    for (int index = 0; index < 3; index++) {
        uint8_t *out = NULL;
        size_t out_len = 0;
        done &= forebay_library_routing_step((const uint8_t *)walk_texts[index].bytes, walk_texts[index].length, &out,
                                             &out_len) == 0;
        forebay_library_release(out);
    }
    return done;
}

/** A consumer's whole walk over JSON: each answer read, and its walk printed into the next call. */
static int step_json_round_trip(void *data) {
    (void)data;
    cJSON *first = json_call(forebay_library_routing_step, &walk_texts[0]);
    json_text second_text = report_text(cJSON_GetObjectItemCaseSensitive(first, "walk"), "thrown", thrown_message);
    cJSON *second = json_call(forebay_library_routing_step, &second_text);
    json_text third_text = report_text(cJSON_GetObjectItemCaseSensitive(second, "walk"), "response", NULL);
    cJSON *third = json_call(forebay_library_routing_step, &third_text);
    int done = third != NULL && strcmp(json_step_kind(third), "done") == 0;
    cJSON_Delete(first);
    cJSON_Delete(second);
    cJSON_Delete(third);
    cJSON_free(second_text.bytes);
    cJSON_free(third_text.bytes);
    return done;
}

static int step_struct(void *data) {
    (void)data;
    const forebay_routing_definition *routing = forebay_library_routing_definition();
    forebay_step_argument start = start_argument();
    forebay_step_answer first = {.size = sizeof first};
    forebay_step_answer second = {.size = sizeof second};
    forebay_step_answer third = {.size = sizeof third};
    int done = routing->step(routing->context, &start, &first, NULL) == SPISOR_OK;
    forebay_report thrown = report_of(FOREBAY_REPORT_THROWN, thrown_message);
    forebay_step_argument reported = report_argument(first.walk, &thrown);
    done = done && routing->step(routing->context, &reported, &second, NULL) == SPISOR_OK;
    forebay_report response = report_of(FOREBAY_REPORT_RESPONSE, NULL);
    forebay_step_argument served = report_argument(second.walk, &response);
    done = done && routing->step(routing->context, &served, &third, NULL) == SPISOR_OK &&
           third.kind == FOREBAY_STEP_DONE;
    routing->release_walk(routing->context, first.walk);
    routing->release_walk(routing->context, second.walk);
    routing->release_step(routing->context, &first);
    routing->release_step(routing->context, &second);
    routing->release_step(routing->context, &third);
    return done;
}

static int measure(void) {
    arrival_text = arrival_text_of(&arrival_headers, "/v1/messages?beta=true", PORT);
    spike_path arrival_paths[] = {
        {"json", arrival_json, NULL},
        {"json-again", arrival_json, NULL},
        {"struct", arrival_struct, NULL},
        {"json-with-encode-decode", arrival_json_round_trip, NULL},
    };
    spike_path step_paths[] = {
        {"json", step_json, NULL},
        {"json-again", step_json, NULL},
        {"struct-with-walk-handle", step_struct, NULL},
        {"json-with-decode-and-walk", step_json_round_trip, NULL},
    };
    int measured = spike_measure("routing.arrival", arrival_paths, 4);
    measured &= spike_measure("routing.step (one walk of three calls)", step_paths, 4);
    cJSON_free(arrival_text.bytes);
    return measured;
}

int main(int argc, char **argv) {
    prepare_walk_texts();
    check();
    if (failures == 0 && argc > 1 && strcmp(argv[1], "measure") == 0 && !measure()) {
        fail("a measured call failed");
    }
    for (int index = 0; index < 3; index++) {
        cJSON_free(walk_texts[index].bytes);
    }
    if (failures == 0) {
        printf("routing definition spike: %s\n", argc > 1 ? "checked and measured" : "checked");
    }
    return failures == 0 ? 0 : 1;
}
