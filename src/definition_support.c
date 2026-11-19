#include "definition_support.h"

#include <math.h>
#include <stdio.h>

#include "spisor/json_value.h"

int forebay_is_text(const char *text) {
    return text != NULL && text[0] != '\0';
}

int forebay_is_finite(double number) {
    return isfinite(number);
}

spisor_status forebay_refuse_member(spisor_refusal *refusal, const char *member, const char *rule) {
    char detail[200];
    char fix[200];
    snprintf(detail, sizeof detail, "%s is not %s", member, rule);
    snprintf(fix, sizeof fix, "set %s to %s", member, rule);
    return spisor_refuse_as(refusal, SPISOR_MALFORMED, detail, fix);
}

spisor_status forebay_json_of_member(const spisor_value *value, const char *member, cJSON **json,
                                     spisor_refusal *refusal) {
    spisor_status status = spisor_json_of_value(value, json);
    if (status == SPISOR_MALFORMED) {
        return forebay_refuse_member(refusal, member, "a well-formed spisor_value");
    }
    return status;
}

static void overwrite_json_texts(cJSON *json) {
    for (cJSON *child = json; child != NULL; child = child->next) {
        if (child->string != NULL) {
            spisor_overwrite(child->string, strlen(child->string));
        }
        if (child->valuestring != NULL) {
            spisor_overwrite(child->valuestring, strlen(child->valuestring));
        }
        overwrite_json_texts(child->child);
    }
}

void forebay_delete_overwritten(cJSON *json) {
    if (json != NULL) {
        overwrite_json_texts(json->child);
        if (json->valuestring != NULL) {
            spisor_overwrite(json->valuestring, strlen(json->valuestring));
        }
    }
    cJSON_Delete(json);
}
