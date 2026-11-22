#ifndef FOREBAY_TEST_ANTHROPIC_SHAPE_CASES_H
#define FOREBAY_TEST_ANTHROPIC_SHAPE_CASES_H

#include <stddef.h>

/**
 * @file
 * The cases of `model_listing` and `model_lookup`, as the argument in JSON and the body the proxy's TypeScript
 * `model-listing.ts` writes for it. They have no vector files: the expected bodies were worked out from that source
 * and are compared by JSON equality, and anthropic_test also pins the exact bytes of a listing.
 */

typedef struct anthropic_shape_case {
    const char *label;
    const char *direction;
    const char *input;
    const char *expected;
} anthropic_shape_case;

static const anthropic_shape_case ANTHROPIC_SHAPE_CASES[] = {
    {"entries are written in order, each with its own name and limits", "modelListing",
     "{\"models\":[{\"id\":\"a\",\"name\":\"A first\",\"context\":100,\"output\":10},{\"id\":\"b\","
     "\"name\":\"B\",\"context\":200000,\"output\":8192},{\"id\":\"c\",\"name\":\"C\",\"context\":200000,"
     "\"output\":8192}]}",
     "{\"data\":[{\"type\":\"model\",\"id\":\"a\",\"display_name\":\"A first\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":100,\"max_tokens\":10},"
     "{\"type\":\"model\",\"id\":\"b\",\"display_name\":\"B\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":200000,\"max_tokens\":8192},"
     "{\"type\":\"model\",\"id\":\"c\",\"display_name\":\"C\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":200000,\"max_tokens\":8192}],\"first_id\":\"a\",\"last_id\":\"c\",\"has_more\":false}"},
    {"an entry's name is its display name", "modelListing",
     "{\"models\":[{\"id\":\"empty\",\"name\":\"empty\",\"context\":5,\"output\":6},{\"id\":\"absent\","
     "\"name\":\"absent\",\"context\":5,\"output\":6}]}",
     "{\"data\":[{\"type\":\"model\",\"id\":\"empty\",\"display_name\":\"empty\","
     "\"created_at\":\"2025-01-01T00:00:00Z\",\"max_input_tokens\":5,\"max_tokens\":6},"
     "{\"type\":\"model\",\"id\":\"absent\",\"display_name\":\"absent\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":5,\"max_tokens\":6}],\"first_id\":\"empty\",\"last_id\":\"absent\",\"has_more\":false}"},
    {"a zero limit is written as a limit", "modelListing",
     "{\"models\":[{\"id\":\"c\",\"name\":\"c\",\"context\":3,\"output\":8},{\"id\":\"o\",\"name\":\"o\","
     "\"context\":7,\"output\":4},{\"id\":\"z\",\"name\":\"z\",\"context\":0,\"output\":0},{\"id\":\"n\","
     "\"name\":\"n\",\"context\":7,\"output\":8}]}",
     "{\"data\":[{\"type\":\"model\",\"id\":\"c\",\"display_name\":\"c\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":3,\"max_tokens\":8},"
     "{\"type\":\"model\",\"id\":\"o\",\"display_name\":\"o\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":7,\"max_tokens\":4},"
     "{\"type\":\"model\",\"id\":\"z\",\"display_name\":\"z\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":0,\"max_tokens\":0},"
     "{\"type\":\"model\",\"id\":\"n\",\"display_name\":\"n\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":7,\"max_tokens\":8}],\"first_id\":\"c\",\"last_id\":\"n\",\"has_more\":false}"},
    {"no entries list no model", "modelListing", "{\"models\":[]}",
     "{\"data\":[],\"first_id\":null,\"last_id\":null,\"has_more\":false}"},
    {"one entry is both the first and the last", "modelListing",
     "{\"models\":[{\"id\":\"only\",\"name\":\"Only\",\"context\":1,\"output\":2}]}",
     "{\"data\":[{\"type\":\"model\",\"id\":\"only\",\"display_name\":\"Only\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":1,\"max_tokens\":2}],\"first_id\":\"only\",\"last_id\":\"only\",\"has_more\":false}"},
    {"a lookup writes the first entry the id names", "modelLookup",
     "{\"id\":\"a\",\"models\":[{\"id\":\"b\",\"name\":\"b\",\"context\":200000,\"output\":8192},"
     "{\"id\":\"a\",\"name\":\"A first\",\"context\":100,\"output\":8192}]}",
     "{\"type\":\"model\",\"id\":\"a\",\"display_name\":\"A first\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":100,\"max_tokens\":8192}"},
    {"a lookup of an id no entry names is the 404 body", "modelLookup",
     "{\"id\":\"missing\",\"models\":[{\"id\":\"a\",\"name\":\"a\",\"context\":1,\"output\":2}]}",
     "{\"type\":\"error\",\"error\":{\"type\":\"not_found_error\",\"message\":\"model not found: missing\"}}"},
    {"a lookup of an id that needs JSON escapes", "modelLookup",
     "{\"id\":\"q\\\"\\\\\\u0001 caf\\u00e9\",\"models\":[]}",
     "{\"type\":\"error\",\"error\":{\"type\":\"not_found_error\",\"message\":\"model not found: q\\\"\\\\\\u0001 "
     "caf\\u00e9\"}}"},
    {"a lookup of the empty id finds nothing", "modelLookup",
     "{\"id\":\"\",\"models\":[{\"id\":\"a\",\"name\":\"a\",\"context\":1,\"output\":2}]}",
     "{\"type\":\"error\",\"error\":{\"type\":\"not_found_error\",\"message\":\"model not found: \"}}"},
    {"entries sharing an id are each written, since which are listed is routing's decision", "modelListing",
     "{\"models\":[{\"id\":\"x\",\"name\":\"X one\",\"context\":1,\"output\":2},{\"id\":\"x\","
     "\"name\":\"X two\",\"context\":3,\"output\":4}]}",
     "{\"data\":[{\"type\":\"model\",\"id\":\"x\",\"display_name\":\"X one\","
     "\"created_at\":\"2025-01-01T00:00:00Z\",\"max_input_tokens\":1,\"max_tokens\":2},"
     "{\"type\":\"model\",\"id\":\"x\",\"display_name\":\"X two\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":3,\"max_tokens\":4}],\"first_id\":\"x\",\"last_id\":\"x\",\"has_more\":false}"},
    {"an entry without limits is written without them", "modelListing",
     "{\"models\":[{\"id\":\"m\",\"name\":\"M\"}]}",
     "{\"data\":[{\"type\":\"model\",\"id\":\"m\",\"display_name\":\"M\","
     "\"created_at\":\"2025-01-01T00:00:00Z\"}],\"first_id\":\"m\",\"last_id\":\"m\",\"has_more\":false}"},
};

#define ANTHROPIC_SHAPE_CASE_COUNT (sizeof ANTHROPIC_SHAPE_CASES / sizeof ANTHROPIC_SHAPE_CASES[0])

#endif
