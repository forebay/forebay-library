#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <dlfcn.h>
#endif

#include <weivel/description.h>
#include <weivel/library.h>

#include "check.h"
#include "declaration_file.h"

/**
 * @file
 * Loads the built forebay-library as a binding would: reads `library.json` with the standard's reader, and for every
 * service and every contract calls the `description` symbol it declares and requires the description to pass the
 * standard's own check and to name `forebay_<id>_definition`, `<id>` a service's id or a contract's capability after
 * `forebay.`, and a service's description to answer the definition its definition function answers, so a description
 * that is declared but not exported, not generated from its own struct, or naming another function, fails here.
 * A contract's description is checked only here: no definition of it exists in this library to call.
 */

typedef void (*exported_function)(void);
typedef const weivel_standard_description *(*describe_function)(void);
typedef const void *(*definition_function)(void);

#ifdef _WIN32
typedef HMODULE library_handle;

static library_handle load_forebay(void) {
    return LoadLibraryA(FOREBAY_LIBRARY_PATH);
}

static exported_function exported_of(library_handle library, const char *symbol) {
    FARPROC address = GetProcAddress(library, symbol);
    return address == NULL ? NULL : (exported_function)address;
}

static void unload_forebay(library_handle library) {
    FreeLibrary(library);
}
#else
typedef void *library_handle;

static library_handle load_forebay(void) {
    return dlopen(FOREBAY_LIBRARY_PATH, RTLD_NOW | RTLD_LOCAL);
}

static exported_function exported_of(library_handle library, const char *symbol) {
    void *address = dlsym(library, symbol);
    exported_function exported = NULL;
    if (address != NULL) {
        memcpy(&exported, &address, sizeof exported);
    }
    return exported;
}

static void unload_forebay(library_handle library) {
    dlclose(library);
}
#endif

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

static void check_definition_name(const char *definition, const char *definition_id, const char *label) {
    char expected[128];
    int written = snprintf(expected, sizeof expected, "forebay_%s_definition", definition_id);
    CHECK(written > 0 && (size_t)written < sizeof expected);
    check_text(definition, expected, label);
}

/** The id a contract's definition is named by: its capability after `forebay.`, NULL for one in no other namespace. */
static const char *contract_definition_id(const char *capability) {
    static const char prefix[] = "forebay.";
    return strncmp(capability, prefix, sizeof prefix - 1) == 0 ? capability + sizeof prefix - 1 : NULL;
}

static describe_function describer_of(library_handle library, const char *symbol) {
    exported_function exported = exported_of(library, symbol);
    return exported == NULL ? NULL : (describe_function)exported;
}

static const char *definition_symbol_of(const weivel_standard_library_service *service) {
    for (size_t index = 0; index < service->function_count; index++) {
        if (service->functions[index].kind == WEIVEL_STANDARD_FUNCTION_DEFINITION) {
            return service->functions[index].symbol;
        }
    }
    return NULL;
}

/** Requires `description` to answer the definition the service's own definition function answers. */
static void check_answers(library_handle forebay, const weivel_standard_description *description,
                          const weivel_standard_library_service *service) {
    const char *definition_symbol = definition_symbol_of(service);
    exported_function definer = definition_symbol == NULL ? NULL : exported_of(forebay, definition_symbol);
    weivel_standard_refusal refusal = WEIVEL_INIT(weivel_standard_refusal);
    if (definer == NULL) {
        fprintf(stderr, "service %s declares no definition function forebay-library exports\n", service->id);
        check_failures++;
        return;
    }
    if (weivel_standard_description_answers(description, ((definition_function)definer)(), &refusal) !=
        WEIVEL_STATUS_OK) {
        fprintf(stderr, "service %s: its description does not answer %s, at \"%s\": %s\n", service->id,
                definition_symbol, refusal.path == NULL ? "" : refusal.path,
                refusal.detail == NULL ? "" : refusal.detail);
        weivel_standard_refusal_release(&refusal);
        check_failures++;
    }
}

/** The description `symbol` answers when it passes the standard's check and names its definition, else NULL. */
static const weivel_standard_description *checked_description(library_handle forebay, const char *symbol,
                                                              const char *definition_id, const char *label) {
    describe_function describer;
    const weivel_standard_description *description;
    weivel_standard_refusal refusal = WEIVEL_INIT(weivel_standard_refusal);
    if (symbol == NULL || definition_id == NULL) {
        fprintf(stderr, "%s declares no description of a forebay definition\n", label);
        check_failures++;
        return NULL;
    }
    describer = describer_of(forebay, symbol);
    if (describer == NULL) {
        fprintf(stderr, "%s declares %s, which forebay-library does not export\n", label, symbol);
        check_failures++;
        return NULL;
    }
    description = describer();
    if (weivel_standard_description_check(description, &refusal) != WEIVEL_STATUS_OK) {
        fprintf(stderr, "%s is refused at \"%s\": %s\n", symbol, refusal.path == NULL ? "" : refusal.path,
                refusal.detail == NULL ? "" : refusal.detail);
        weivel_standard_refusal_release(&refusal);
        check_failures++;
        return NULL;
    }
    check_definition_name(description->definition, definition_id, label);
    return description;
}

static void every_declared_description_is_exported_and_checks(void) {
    weivel_standard_library library = WEIVEL_INIT(weivel_standard_library);
    library_handle forebay = load_forebay();
    CHECK(forebay != NULL);
    if (forebay == NULL) {
        fprintf(stderr, "could not load %s\n", FOREBAY_LIBRARY_PATH);
        return;
    }
    if (read_library(&library)) {
        CHECK(library.service_count > 0);
        for (size_t index = 0; index < library.service_count; index++) {
            const weivel_standard_library_service *service = &library.services[index];
            const weivel_standard_description *description =
                checked_description(forebay, library.descriptions[index], service->id, service->id);
            if (description != NULL) {
                check_answers(forebay, description, service);
            }
        }
        CHECK(library.contract_count > 0);
        for (size_t index = 0; index < library.contract_count; index++) {
            const weivel_standard_library_contract *contract = &library.contracts[index];
            checked_description(forebay, contract->description, contract_definition_id(contract->capability),
                                contract->capability);
        }
        weivel_standard_library_release(&library);
    }
    unload_forebay(forebay);
}

static const weivel_standard_described_operation *operation_named(const weivel_standard_description *description,
                                                                  const char *name) {
    for (size_t index = 0; index < description->operation_count; index++) {
        if (strcmp(description->operations[index].name, name) == 0) {
            return &description->operations[index];
        }
    }
    return NULL;
}

static const weivel_standard_described_member *member_named(const weivel_standard_description *description,
                                                            const char *type, const char *name) {
    for (size_t index = 0; index < description->type_count; index++) {
        const weivel_standard_described_type *described = &description->types[index];
        if (strcmp(described->name, type) != 0) {
            continue;
        }
        for (size_t member = 0; member < described->member_count; member++) {
            if (strcmp(described->members[member].name, name) == 0) {
                return &described->members[member];
            }
        }
    }
    return NULL;
}

static void the_provider_description_carries_the_token_and_the_cancel_operation(void) {
    library_handle forebay = load_forebay();
    describe_function describer;
    const weivel_standard_description *description;
    const weivel_standard_described_operation *cancel;
    const weivel_standard_described_member *token;
    CHECK(forebay != NULL);
    if (forebay == NULL) {
        return;
    }
    describer = describer_of(forebay, "forebay_provider_definition_description");
    CHECK(describer != NULL);
    description = describer == NULL ? NULL : describer();
    CHECK(description != NULL);
    if (description != NULL) {
        cancel = operation_named(description, "cancel");
        CHECK(cancel != NULL);
        if (cancel != NULL) {
            CHECK(cancel->returns == WEIVEL_STANDARD_DESCRIBED_STATUS);
            CHECK(cancel->parameter_count == 3);
            CHECK(cancel->parameters[1].kind == WEIVEL_STANDARD_DESCRIBED_POINTER);
            CHECK(cancel->parameters[1].role == WEIVEL_STANDARD_DESCRIBED_INPUT);
            CHECK(cancel->parameters[1].type != NULL &&
                  strcmp(cancel->parameters[1].type, "forebay_provider_cancel_argument") == 0);
        }
        token = member_named(description, "forebay_provider_handle_argument", "token");
        CHECK(token != NULL);
        if (token != NULL) {
            CHECK(token->kind == WEIVEL_STANDARD_DESCRIBED_U64);
            CHECK(token->size == 8);
        }
        token = member_named(description, "forebay_provider_cancel_argument", "token");
        CHECK(token != NULL);
        if (token != NULL) {
            CHECK(token->kind == WEIVEL_STANDARD_DESCRIBED_U64);
        }
    }
    unload_forebay(forebay);
}

int main(void) {
    every_declared_description_is_exported_and_checks();
    the_provider_description_carries_the_token_and_the_cancel_operation();
    return check_summary("description_test");
}
