#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <weivel/c/host.h>
#include <weivel/c/host/library.h>
#include <weivel/c/standard.h>
#include <weivel/library.h>

#include "check.h"
#include "declaration_file.h"
#include "forebay/anthropic.h"
#include "forebay/routing.h"

/**
 * @file
 * Loads the built forebay-library as a weivel host loads a C library: reads `library.json` with the standard, hands it
 * to `weivel/c`'s host on the pinned core, which runs the standard's check on every description the declaration names
 * before it registers anything, resolves `routing` as that host's own context gets a service, and calls `arrival`
 * through the very definition the host registered, and `anthropic` the same way, writing an error body. The same
 * declaration with one service's definition function pointed at another's is refused.
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
    options.description_check.check = weivel_c_standard_check_description;
    CHECK(weivel_c_host_create(&options, &host, &failure) == WEIVEL_C_STATUS_OK && host != NULL);
    return host;
}

static int load_forebay(weivel_c_host *host, const weivel_standard_library *declaration) {
    weivel_c_library *library = NULL;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    weivel_c_status status = weivel_c_host_load_library(host, FOREBAY_LIBRARY_PATH, declaration, &library, &failure,
                                                        NULL);
    if (status != WEIVEL_C_STATUS_OK) {
        fprintf(stderr, "the host did not load %s: status %d, missing %s, wrong kind %s, refused %s, description %s\n",
                FOREBAY_LIBRARY_PATH, (int)status, failure.missing_symbol == NULL ? "-" : failure.missing_symbol,
                failure.wrong_kind == NULL ? "-" : failure.wrong_kind,
                failure.refused_definition == NULL ? "-" : failure.refused_definition,
                failure.refused_description == NULL ? "-" : failure.refused_description);
        check_failures++;
    }
    return status == WEIVEL_C_STATUS_OK;
}

static void *service_of(weivel_c_host *host, const char *id) {
    weivel_c_context *context = NULL;
    void *service = NULL;
    CHECK(weivel_c_host_context(host, &context) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_context_get_service(context, id, &service, NULL) == WEIVEL_C_STATUS_OK);
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

static void write_error_through(const forebay_anthropic_definition *anthropic) {
    static const char expected[] =
        "{\"type\":\"error\",\"error\":{\"type\":\"api_error\",\"message\":\"upstream failed\"}}";
    forebay_anthropic_error_argument argument = SPISOR_INIT(forebay_anthropic_error_argument);
    forebay_anthropic_body_answer answer = SPISOR_INIT(forebay_anthropic_body_answer);
    spisor_refusal refusal = SPISOR_INIT(spisor_refusal);
    CHECK(anthropic->size >= sizeof(forebay_anthropic_definition));
    argument.reply_kind = FOREBAY_REPLY_API_ERROR;
    argument.message = "upstream failed";
    CHECK(anthropic->error_envelope(anthropic->context, &argument, &answer, &refusal) == SPISOR_OK);
    CHECK(answer.length == sizeof expected - 1 && answer.bytes != NULL &&
          memcmp(answer.bytes, expected, sizeof expected - 1) == 0);
    anthropic->release_body(anthropic->context, &answer);
    anthropic->release_refusal(anthropic->context, &refusal);
    CHECK(answer.storage == NULL && refusal.storage == NULL);
}

static void destroy_host(weivel_c_host *host) {
    uint32_t finished = 0;
    CHECK(weivel_c_host_destroy(host, &finished) == WEIVEL_C_STATUS_OK && finished == 1);
    CHECK(weivel_c_host_free(host) == WEIVEL_C_STATUS_OK);
}

static void services_work_through_the_definitions_the_host_registered(void) {
    weivel_standard_library declaration = WEIVEL_INIT(weivel_standard_library);
    weivel_c_host *host;
    const forebay_routing_definition *routing;
    const forebay_anthropic_definition *anthropic;
    if (!read_library(&declaration)) {
        return;
    }
    host = create_host();
    if (host != NULL && load_forebay(host, &declaration)) {
        routing = service_of(host, "routing");
        CHECK(routing != NULL);
        if (routing != NULL) {
            arrive_through(routing);
        }
        anthropic = service_of(host, "anthropic");
        CHECK(anthropic != NULL);
        if (anthropic != NULL) {
            write_error_through(anthropic);
        }
    }
    if (host != NULL) {
        destroy_host(host);
    }
    weivel_standard_library_release(&declaration);
}

/**
 * Points the definition function of the service `id` among `services` at `symbol`, through a copy of its functions
 * the caller frees; NULL when no such service declares a definition, or out of memory.
 */
static weivel_standard_library_function *serve_by(weivel_standard_library_service *services, size_t service_count,
                                                  const char *id, const char *symbol) {
    for (size_t index = 0; index < service_count; index++) {
        weivel_standard_library_service *service = &services[index];
        weivel_standard_library_function *functions;
        if (strcmp(service->id, id) != 0) {
            continue;
        }
        functions = malloc(service->function_count * sizeof *functions);
        if (functions == NULL) {
            return NULL;
        }
        memcpy(functions, service->functions, service->function_count * sizeof *functions);
        for (size_t function = 0; function < service->function_count; function++) {
            if (functions[function].kind == WEIVEL_STANDARD_FUNCTION_DEFINITION) {
                functions[function].symbol = symbol;
                service->functions = functions;
                return functions;
            }
        }
        free(functions);
    }
    return NULL;
}

static void check_load_refused(const weivel_standard_library *declaration, const char *description) {
    weivel_c_host *host = create_host();
    weivel_c_library *library = NULL;
    weivel_c_library_load_failure failure = WEIVEL_INIT(weivel_c_library_load_failure);
    if (host == NULL) {
        return;
    }
    CHECK(weivel_c_host_load_library(host, FOREBAY_LIBRARY_PATH, declaration, &library, &failure, NULL) ==
          WEIVEL_C_STATUS_MISUSE);
    CHECK(library == NULL);
    check_text(failure.refused_description, description, "the description refused");
    CHECK(weivel_c_host_destroy(host, NULL) == WEIVEL_C_STATUS_OK);
    CHECK(weivel_c_host_free(host) == WEIVEL_C_STATUS_OK);
}

/**
 * `routing` served by `accounts`' definition under routing's own description: the description passes the standard's
 * check, so only the check that a description answers its service's definition refuses it, before anything is
 * registered.
 */
static void a_definition_served_under_another_description_is_refused(void) {
    weivel_standard_library declaration = WEIVEL_INIT(weivel_standard_library);
    weivel_standard_library swapped;
    weivel_standard_library_service *services;
    weivel_standard_library_function *functions = NULL;
    if (!read_library(&declaration)) {
        return;
    }
    services = malloc(declaration.service_count * sizeof *services);
    CHECK(services != NULL);
    if (services != NULL) {
        memcpy(services, declaration.services, declaration.service_count * sizeof *services);
        functions = serve_by(services, declaration.service_count, "routing", "forebay_accounts_definition_of");
        CHECK(functions != NULL);
    }
    if (functions != NULL) {
        swapped = declaration;
        swapped.services = services;
        check_load_refused(&swapped, "forebay_routing_definition_description");
    }
    free(functions);
    free(services);
    weivel_standard_library_release(&declaration);
}

/**
 * With the argument `another-definition`, runs the refusal alone: a process loads forebay-library once, since ASan on
 * macOS reports a library loaded again after its unload as an ODR violation of its own globals.
 */
int main(int argument_count, char **arguments) {
    if (argument_count == 2 && strcmp(arguments[1], "another-definition") == 0) {
        a_definition_served_under_another_description_is_refused();
    } else {
        services_work_through_the_definitions_the_host_registered();
    }
    return check_summary("weivel_host_load_test");
}
