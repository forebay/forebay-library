#include "anthropic_stringify.h"

#include <math.h>
#include <string.h>

#include "spisor/allocation.h"
#include "spisor/javascript.h"
#include "spisor/json_write.h"

#define INITIAL_CAPACITY 256

typedef struct text_buffer {
    char *data;
    size_t length;
    size_t capacity;
    int out_of_memory;
} text_buffer;

static void append_bytes(text_buffer *buffer, const char *bytes, size_t count) {
    if (buffer->out_of_memory) {
        return;
    }
    if (buffer->length + count + 1 > buffer->capacity) {
        size_t capacity = buffer->capacity == 0 ? INITIAL_CAPACITY : buffer->capacity;
        while (buffer->length + count + 1 > capacity) {
            capacity *= 2;
        }
        char *grown = spisor_memory_reallocate(buffer->data, capacity);
        if (grown == NULL) {
            buffer->out_of_memory = 1;
            return;
        }
        buffer->data = grown;
        buffer->capacity = capacity;
    }
    memcpy(buffer->data + buffer->length, bytes, count);
    buffer->length += count;
    buffer->data[buffer->length] = '\0';
}

static void append_text(text_buffer *buffer, const char *text) {
    append_bytes(buffer, text, strlen(text));
}

static const char *short_escape(unsigned char byte) {
    switch (byte) {
    case '"': return "\\\"";
    case '\\': return "\\\\";
    case '\b': return "\\b";
    case '\f': return "\\f";
    case '\n': return "\\n";
    case '\r': return "\\r";
    case '\t': return "\\t";
    default: return NULL;
    }
}

static void append_control_escape(text_buffer *buffer, unsigned char byte) {
    static const char hex_digits[] = "0123456789abcdef";
    char escape[6] = {'\\', 'u', '0', '0', hex_digits[byte >> 4], hex_digits[byte & 0x0f]};
    append_bytes(buffer, escape, sizeof escape);
}

static void append_quoted(text_buffer *buffer, const char *text) {
    append_bytes(buffer, "\"", 1);
    for (const char *cursor = text; *cursor != '\0'; cursor++) {
        unsigned char byte = (unsigned char)*cursor;
        const char *escape = short_escape(byte);
        if (escape != NULL) {
            append_text(buffer, escape);
        } else if (byte < 0x20) {
            append_control_escape(buffer, byte);
        } else {
            append_bytes(buffer, cursor, 1);
        }
    }
    append_bytes(buffer, "\"", 1);
}

static void append_number(text_buffer *buffer, const cJSON *number) {
    if (!isfinite(number->valuedouble)) {
        append_text(buffer, "null");
        return;
    }
    char *text = spisor_javascript_string(number);
    if (text == NULL) {
        buffer->out_of_memory = 1;
        return;
    }
    append_text(buffer, text);
    spisor_memory_release(text);
}

static void append_value(text_buffer *buffer, const cJSON *value);

static void append_array(text_buffer *buffer, const cJSON *array) {
    append_bytes(buffer, "[", 1);
    const cJSON *element = NULL;
    cJSON_ArrayForEach(element, array) {
        append_value(buffer, element);
        if (element->next != NULL) {
            append_bytes(buffer, ",", 1);
        }
    }
    append_bytes(buffer, "]", 1);
}

static void append_object(text_buffer *buffer, const cJSON *object) {
    size_t count = 0;
    const cJSON **ordered = spisor_json_members_in_javascript_order(object, &count);
    if (count > 0 && ordered == NULL) {
        buffer->out_of_memory = 1;
        return;
    }
    append_bytes(buffer, "{", 1);
    for (size_t position = 0; position < count; position++) {
        append_quoted(buffer, ordered[position]->string);
        append_bytes(buffer, ":", 1);
        append_value(buffer, ordered[position]);
        if (position + 1 < count) {
            append_bytes(buffer, ",", 1);
        }
    }
    append_bytes(buffer, "}", 1);
    spisor_memory_release(ordered);
}

static void append_value(text_buffer *buffer, const cJSON *value) {
    if (cJSON_IsString(value)) {
        append_quoted(buffer, value->valuestring);
    } else if (cJSON_IsNumber(value)) {
        append_number(buffer, value);
    } else if (cJSON_IsTrue(value)) {
        append_text(buffer, "true");
    } else if (cJSON_IsFalse(value)) {
        append_text(buffer, "false");
    } else if (cJSON_IsArray(value)) {
        append_array(buffer, value);
    } else if (cJSON_IsObject(value)) {
        append_object(buffer, value);
    } else {
        append_text(buffer, "null");
    }
}

char *anthropic_stringify(const cJSON *value, size_t *length) {
    text_buffer buffer = {NULL, 0, 0, 0};
    append_value(&buffer, value);
    if (buffer.out_of_memory) {
        spisor_memory_release(buffer.data);
        return NULL;
    }
    *length = buffer.length;
    return buffer.data;
}
