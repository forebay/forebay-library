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
    {"two providers sharing an id list it once as the first offers it", "modelListing",
     "{\"defaultContext\":200000,\"defaultOutput\":8192,\"providers\":["
     "[{\"id\":\"a\",\"name\":\"A first\",\"context\":100,\"output\":10},{\"id\":\"b\",\"name\":\"B\"}],"
     "[{\"id\":\"a\",\"name\":\"A second\",\"context\":1,\"output\":1},{\"id\":\"b\",\"name\":\"B second\"},"
     "{\"id\":\"c\",\"name\":\"C\"}]]}",
     "{\"data\":[{\"type\":\"model\",\"id\":\"a\",\"display_name\":\"A first\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":100,\"max_tokens\":10},"
     "{\"type\":\"model\",\"id\":\"b\",\"display_name\":\"B\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":200000,\"max_tokens\":8192},"
     "{\"type\":\"model\",\"id\":\"c\",\"display_name\":\"C\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":200000,\"max_tokens\":8192}],\"first_id\":\"a\",\"last_id\":\"c\",\"has_more\":false}"},
    {"an empty name and an absent name list the model under its id", "modelListing",
     "{\"defaultContext\":5,\"defaultOutput\":6,"
     "\"providers\":[[{\"id\":\"empty\",\"name\":\"\"},{\"id\":\"absent\"}]]}",
     "{\"data\":[{\"type\":\"model\",\"id\":\"empty\",\"display_name\":\"empty\","
     "\"created_at\":\"2025-01-01T00:00:00Z\",\"max_input_tokens\":5,\"max_tokens\":6},"
     "{\"type\":\"model\",\"id\":\"absent\",\"display_name\":\"absent\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":5,\"max_tokens\":6}],\"first_id\":\"empty\",\"last_id\":\"absent\",\"has_more\":false}"},
    {"a context alone and an output alone each default the other, and a zero is a limit", "modelListing",
     "{\"defaultContext\":7,\"defaultOutput\":8,\"providers\":[[{\"id\":\"c\",\"context\":3},{\"id\":\"o\","
     "\"output\":4},{\"id\":\"z\",\"context\":0,\"output\":0},{\"id\":\"n\",\"context\":null,\"output\":null}]]}",
     "{\"data\":[{\"type\":\"model\",\"id\":\"c\",\"display_name\":\"c\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":3,\"max_tokens\":8},"
     "{\"type\":\"model\",\"id\":\"o\",\"display_name\":\"o\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":7,\"max_tokens\":4},"
     "{\"type\":\"model\",\"id\":\"z\",\"display_name\":\"z\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":0,\"max_tokens\":0},"
     "{\"type\":\"model\",\"id\":\"n\",\"display_name\":\"n\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":7,\"max_tokens\":8}],\"first_id\":\"c\",\"last_id\":\"n\",\"has_more\":false}"},
    {"no providers list no model", "modelListing", "{\"defaultContext\":1,\"defaultOutput\":2,\"providers\":[]}",
     "{\"data\":[],\"first_id\":null,\"last_id\":null,\"has_more\":false}"},
    {"providers that serve nothing list no model", "modelListing",
     "{\"defaultContext\":1,\"defaultOutput\":2,\"providers\":[[],[]]}",
     "{\"data\":[],\"first_id\":null,\"last_id\":null,\"has_more\":false}"},
    {"one model is both the first and the last", "modelListing",
     "{\"defaultContext\":1,\"defaultOutput\":2,\"providers\":[[{\"id\":\"only\",\"name\":\"Only\"}]]}",
     "{\"data\":[{\"type\":\"model\",\"id\":\"only\",\"display_name\":\"Only\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":1,\"max_tokens\":2}],\"first_id\":\"only\",\"last_id\":\"only\",\"has_more\":false}"},
    {"a lookup finds the model as the first provider offers it", "modelLookup",
     "{\"id\":\"a\",\"defaultContext\":200000,\"defaultOutput\":8192,\"providers\":["
     "[{\"id\":\"b\"},{\"id\":\"a\",\"name\":\"A first\",\"context\":100}],[{\"id\":\"a\",\"name\":\"A second\"}]]}",
     "{\"type\":\"model\",\"id\":\"a\",\"display_name\":\"A first\",\"created_at\":\"2025-01-01T00:00:00Z\","
     "\"max_input_tokens\":100,\"max_tokens\":8192}"},
    {"a lookup of an id no provider offers is the 404 body", "modelLookup",
     "{\"id\":\"missing\",\"defaultContext\":1,\"defaultOutput\":2,\"providers\":[[{\"id\":\"a\"}]]}",
     "{\"type\":\"error\",\"error\":{\"type\":\"not_found_error\",\"message\":\"model not found: missing\"}}"},
    {"a lookup of an id that needs JSON escapes", "modelLookup",
     "{\"id\":\"q\\\"\\\\\\u0001 caf\\u00e9\",\"defaultContext\":1,\"defaultOutput\":2,\"providers\":[]}",
     "{\"type\":\"error\",\"error\":{\"type\":\"not_found_error\",\"message\":\"model not found: q\\\"\\\\\\u0001 "
     "caf\\u00e9\"}}"},
    {"a lookup of the empty id finds nothing", "modelLookup",
     "{\"id\":\"\",\"defaultContext\":1,\"defaultOutput\":2,\"providers\":[[{\"id\":\"a\"}]]}",
     "{\"type\":\"error\",\"error\":{\"type\":\"not_found_error\",\"message\":\"model not found: \"}}"},
};

#define ANTHROPIC_SHAPE_CASE_COUNT (sizeof ANTHROPIC_SHAPE_CASES / sizeof ANTHROPIC_SHAPE_CASES[0])

#endif
