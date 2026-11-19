#include "definition_support.h"

#include <math.h>
#include <stdio.h>

#include "spisor/json_value.h"

int forebay_has_shape(uint32_t size, size_t shape) {
    return (size_t)size >= shape;
}

int forebay_is_text(const char *text) {
    return text != NULL && text[0] != '\0';
}

int forebay_is_finite(double number) {
    return isfinite(number);
}

spisor_status forebay_refuse_as(spisor_refusal *refusal, spisor_status status, const char *detail, const char *fix) {
    spisor_status refused = spisor_refuse(refusal, detail, fix);
    return refused == SPISOR_OUT_OF_MEMORY ? SPISOR_OUT_OF_MEMORY : status;
}

spisor_status forebay_refuse_shape(spisor_refusal *refusal, const char *operation) {
    char detail[160];
    snprintf(detail, sizeof detail, "%s takes an argument and an empty answer, each reaching through its first shape",
             operation);
    return forebay_refuse_as(refusal, SPISOR_MALFORMED, detail,
                             "pass both, each with its size set and every other field zero, as SPISOR_INIT leaves it");
}

spisor_status forebay_refuse_member(spisor_refusal *refusal, const char *member, const char *rule) {
    char detail[200];
    char fix[200];
    snprintf(detail, sizeof detail, "%s is not %s", member, rule);
    snprintf(fix, sizeof fix, "set %s to %s", member, rule);
    return forebay_refuse_as(refusal, SPISOR_MALFORMED, detail, fix);
}

void forebay_release_refusal(void *context, spisor_refusal *refusal) {
    (void)context;
    spisor_refusal_release(refusal);
}

spisor_status forebay_json_of_member(const spisor_value *value, const char *member, cJSON **json,
                                     spisor_refusal *refusal) {
    spisor_status status = spisor_json_of_value(value, json);
    if (status == SPISOR_MALFORMED) {
        return forebay_refuse_member(refusal, member, "a well-formed spisor_value");
    }
    return status;
}

void forebay_overwrite(void *bytes, size_t length) {
    volatile unsigned char *cursor = bytes;
    for (size_t index = 0; index < length; index++) {
        cursor[index] = 0;
    }
}

static void overwrite_json_texts(cJSON *json) {
    for (cJSON *child = json; child != NULL; child = child->next) {
        if (child->string != NULL) {
            forebay_overwrite(child->string, strlen(child->string));
        }
        if (child->valuestring != NULL) {
            forebay_overwrite(child->valuestring, strlen(child->valuestring));
        }
        overwrite_json_texts(child->child);
    }
}

void forebay_delete_overwritten(cJSON *json) {
    if (json != NULL) {
        overwrite_json_texts(json->child);
        if (json->valuestring != NULL) {
            forebay_overwrite(json->valuestring, strlen(json->valuestring));
        }
    }
    cJSON_Delete(json);
}
