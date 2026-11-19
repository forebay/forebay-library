#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <weivel/c/host.h>
#include <weivel/c/host/library.h>
#include <weivel/library.h>

#include "check.h"
#include "declaration_file.h"
#include "forebay/routing.h"

/**
 * @file
 * Loads the built forebay-library as a weivel host loads a C library: reads `library.json` with the standard, hands it
 * to `weivel/c`'s host on the pinned core, resolves `routing` as that host's own context gets a service, and calls
 * `arrival` through the very definition the host registered.
 */

static int read_library(weivel_standard_library *library) {
    size_t length = 0;
    char *text = read_declaration_file(&length);
    weivel_status status;
    CHECK(text != NULL);
    if (text == NULL) {
        return 0;
    }
    status = weivel_standard_read_library(text, length, library, NULL);
    free(text);
    CHECK(status == WEIVEL_STATUS_OK);
    return status == WEIVEL_STATUS_OK;
}

static weivel_c_host *create_host(void) {
    weivel_c_host_options options = WEIVEL_INIT(weivel_c_host_options);
    weivel_c_core_load_failure failure = WEIVEL_INIT(weivel_c_core_load_failure);
    weivel_c_host *host = NULL;
    options.core_path = WEIVEL_CORE_PATH;
    CHECK(weivel_c_host_create(&options, &host, &failure) == WEIVEL_C_STATUS_OK && host != NULL);
    return host;
}

static int load_forebay(weivel_c_host *host, const weivel_standard_library *declaration) {
    weivel_c_library *library = NULL;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    weivel_c_status status = weivel_c_host_load_library(host, FOREBAY_LIBRARY_PATH, declaration, &library, &failure,
                                                        NULL);
    if (status != WEIVEL_C_STATUS_OK) {
        fprintf(stderr, "the host did not load %s: status %d, missing %s, wrong kind %s, refused %s\n",
                FOREBAY_LIBRARY_PATH, (int)status, failure.missing_symbol == NULL ? "-" : failure.missing_symbol,
                failure.wrong_kind == NULL ? "-" : failure.wrong_kind,
                failure.refused_definition == NULL ? "-" : failure.refused_definition);
        check_failures++;
    }
    return status == WEIVEL_C_STATUS_OK;
}

static const forebay_routing_definition *routing_of(weivel_c_host *host) {
    weivel_c_context *context = NULL;
    void *service = NULL;
    CHECK(weivel_c_host_context(host, &context) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(context, "routing", &service, NULL) == WEIVEL_C_STATUS_OK);
    return service;
}

static void arrive_through(const forebay_routing_definition *routing) {
    forebay_pair host = {"host", "127.0.0.1:4000"};
    forebay_arrival_argument argument = SPISOR_INIT(forebay_arrival_argument);
    forebay_arrival_answer answer = SPISOR_INIT(forebay_arrival_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    CHECK(routing->size >= sizeof(forebay_routing_definition));
    argument.path = "/health";
    argument.headers = &host;
    argument.header_count = 1;
    argument.port = 4000;
    CHECK(routing->arrival(routing->context, &argument, &answer, &refusal) == SPISOR_OK);
    CHECK(answer.kind == FOREBAY_ARRIVAL_HEALTH && answer.status == 200);
    check_text(answer.body, "ok", "body");
    routing->release_arrival_answer(routing->context, &answer);
    routing->release_refusal(routing->context, &refusal);
    CHECK(answer.storage == NULL && refusal.storage == NULL);
}

static void destroy_host(weivel_c_host *host) {
    uint32_t finished = 0;
    CHECK(weivel_c_host_destroy(host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_host_free(host) == WEIVEL_C_STATUS_OK);
}

static void routing_arrives_through_the_definition_the_host_registered(void) {
    weivel_standard_library declaration = WEIVEL_INIT(weivel_standard_library);
    weivel_c_host *host;
    const forebay_routing_definition *routing;
    if (!read_library(&declaration)) {
        return;
    }
    host = create_host();
    if (host != NULL && load_forebay(host, &declaration)) {
        routing = routing_of(host);
        CHECK(routing != NULL);
        if (routing != NULL) {
            arrive_through(routing);
        }
    }
    if (host != NULL) {
        destroy_host(host);
    }
    weivel_standard_library_release(&declaration);
}

int main(void) {
    routing_arrives_through_the_definition_the_host_registered();
    return check_summary("weivel_host_load_test");
}
