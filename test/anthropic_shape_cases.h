#ifndef FOREBAY_TEST_ANTHROPIC_SHAPE_CASES_H
#define FOREBAY_TEST_ANTHROPIC_SHAPE_CASES_H

#include <stddef.h>

/**
 * @file
 * The cases of the wire shapes, `error_envelope` and `model_listing`, as the argument in JSON and the body the proxy's
 * TypeScript writes for it, which is `replies.ts` for the error and `model-listing.ts` for the listing. They have no
 * vector files: the expected bodies were worked out from those two sources and are compared by JSON equality, and
 * anthropic_test also pins the exact bytes of one of each.
 */

typedef struct anthropic_shape_case {
    const char *label;
    const char *direction;
    const char *input;
    const char *expected;
} anthropic_shape_case;

static const anthropic_shape_case ANTHROPIC_SHAPE_CASES[] = {
    {"the error the proxy answers for a failed upstream", "errorEnvelope",
     "{\"errorType\":\"api_error\",\"message\":\"upstream failed\"}",
     "{\"type\":\"error\",\"error\":{\"type\":\"api_error\",\"message\":\"upstream failed\"}}"},
    {"the error the proxy writes for itself", "errorEnvelope",
     "{\"errorType\":\"loader_proxy_error\",\"message\":\"no provider serves claude-x\"}",
     "{\"type\":\"error\",\"error\":{\"type\":\"loader_proxy_error\",\"message\":\"no provider serves claude-x\"}}"},
    {"an error type no one uses yet is written as given", "errorEnvelope",
     "{\"errorType\":\"overloaded_error\",\"message\":\"\"}",
     "{\"type\":\"error\",\"error\":{\"type\":\"overloaded_error\",\"message\":\"\"}}"},
    {"a message with a quote, a backslash, a control character and non-ASCII text", "errorEnvelope",
     "{\"errorType\":\"api_error\",\"message\":\"say \\\"hi\\\" \\\\ \\u0001 \\n caf\\u00e9 \\ud83d\\ude00\"}",
     "{\"type\":\"error\",\"error\":{\"type\":\"api_error\",\"message\":\"say \\\"hi\\\" \\\\ \\u0001 \\n caf\\u00e9 "
     "\\ud83d\\ude00\"}}"},
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
};

#define ANTHROPIC_SHAPE_CASE_COUNT (sizeof ANTHROPIC_SHAPE_CASES / sizeof ANTHROPIC_SHAPE_CASES[0])

#endif
