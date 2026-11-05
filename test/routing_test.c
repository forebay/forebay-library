#include "support.h"

int32_t routing_tiers(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t routing_model_map(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t routing_resolve(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t routing_outcome(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);
int32_t routing_environment(const uint8_t *in, size_t in_len, uint8_t **out, size_t *out_len);

typedef struct named_function {
    const char *name;
    apps_function function;
} named_function;

static const named_function functions[] = {
    {"tiers", routing_tiers},
    {"modelMap", routing_model_map},
    {"resolve", routing_resolve},
    {"outcome", routing_outcome},
    {"environment", routing_environment},
};

static char *read_text(const char *path) {
    FILE *file = fopen(path, "rb");
    if (file == NULL) {
        return NULL;
    }
    fseek(file, 0, SEEK_END);
    long size = ftell(file);
    fseek(file, 0, SEEK_SET);
    char *text = malloc((size_t)size + 1);
    size_t read = text == NULL ? 0 : fread(text, 1, (size_t)size, file);
    fclose(file);
    if (text != NULL) {
        text[read] = '\0';
    }
    return text;
}

static apps_function find_function(const char *name) {
    for (size_t index = 0; index < sizeof(functions) / sizeof(functions[0]); index++) {
        if (strcmp(functions[index].name, name) == 0) {
            return functions[index].function;
        }
    }
    return NULL;
}

/** Lays out the vector's files in a fresh directory and points the argument's userHome at it. */
static void lay_out_files(cJSON *argument, const cJSON *files) {
    char *home = support_scratch();
    const cJSON *file = NULL;
    cJSON_ArrayForEach(file, files) {
        char *path = support_join(home, file->string);
        support_write(path, file->valuestring);
        free(path);
    }
    cJSON_DeleteItemFromObjectCaseSensitive(argument, "userHome");
    cJSON_AddStringToObject(argument, "userHome", home);
    free(home);
}

static void run_vector(const char *path) {
    char *text = read_text(path);
    cJSON *vector = text == NULL ? NULL : cJSON_Parse(text);
    free(text);
    const cJSON *name = cJSON_GetObjectItemCaseSensitive(vector, "function");
    cJSON *argument = cJSON_DetachItemFromObjectCaseSensitive(vector, "argument");
    const cJSON *result = cJSON_GetObjectItemCaseSensitive(vector, "result");
    apps_function function = cJSON_IsString(name) ? find_function(name->valuestring) : NULL;
    if (function == NULL || argument == NULL || result == NULL) {
        support_fail(path, "not a vector: needs a known function, an argument and a result");
    } else {
        const cJSON *files = cJSON_GetObjectItemCaseSensitive(vector, "files");
        if (files != NULL) {
            lay_out_files(argument, files);
        }
        char *input = cJSON_PrintUnformatted(argument);
        char *expected = cJSON_PrintUnformatted(result);
        support_answer answer = support_call_raw(function, input);
        expect_json(&answer, 0, expected, path);
        free(input);
        free(expected);
    }
    cJSON_Delete(argument);
    cJSON_Delete(vector);
}

static int run_every_vector(void) {
    char *list = read_text(ROUTING_VECTOR_LIST);
    if (list == NULL) {
        support_fail("vector list", ROUTING_VECTOR_LIST);
        return 0;
    }
    int count = 0;
    for (char *line = strtok(list, "\r\n"); line != NULL; line = strtok(NULL, "\r\n")) {
        if (line[0] != '\0') {
            run_vector(line);
            count++;
        }
    }
    free(list);
    return count;
}

static void refuses_a_malformed_call(void) {
    support_answer not_object = support_call_raw(routing_tiers, "[]");
    expect_failure(&not_object, "tiers with an array argument");
    support_answer no_models = support_call_raw(routing_tiers, "{\"profile\":{}}");
    expect_failure(&no_models, "tiers without models");
    support_answer no_regex = support_call_raw(
        routing_tiers,
        "{\"profile\":{\"tierSourceProvider\":\"p\",\"tierOrder\":[],\"tierFallback\":[]},\"models\":{}}");
    expect_failure(&no_regex, "tiers with a profile lacking tierRegex");
    support_answer bad_port = support_call_raw(
        routing_environment,
        "{\"profile\":{\"envPrefix\":\"A\"},\"map\":{},\"port\":0,\"providersEnabled\":true,\"environment\":{},"
        "\"userHome\":\"\"}");
    expect_failure(&bad_port, "environment with port 0");
    support_answer not_json = support_call_raw(routing_outcome, "{\"status\":");
    expect_failure(&not_json, "outcome with broken json");
}

int main(void) {
    int vectors = run_every_vector();
    if (vectors == 0) {
        support_fail("vectors", "no routing vector was found");
    }
    refuses_a_malformed_call();
    printf("routing_test: %d vectors\n", vectors);
    return support_summary("routing_test");
}
