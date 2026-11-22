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
 * `forebay.`, so a description that is declared but not exported, or not generated from its own struct, fails here.
 * A contract's description is checked only here: no definition of it exists in this library to call.
 */

typedef const weivel_standard_description *(*describe_function)(void);

#ifdef _WIN32
typedef HMODULE library_handle;

static library_handle load_forebay(void) {
    return LoadLibraryA(FOREBAY_LIBRARY_PATH);
}

static describe_function describer_of(library_handle library, const char *symbol) {
    FARPROC address = GetProcAddress(library, symbol);
    return address == NULL ? NULL : (describe_function)(void (*)(void))address;
}

static void unload_forebay(library_handle library) {
    FreeLibrary(library);
}
#else
typedef void *library_handle;

static library_handle load_forebay(void) {
    return dlopen(FOREBAY_LIBRARY_PATH, RTLD_NOW | RTLD_LOCAL);
}

static describe_function describer_of(library_handle library, const char *symbol) {
    void *address = dlsym(library, symbol);
    describe_function describer = NULL;
    if (address != NULL) {
        memcpy(&describer, &address, sizeof describer);
    }
    return describer;
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

static void check_description(library_handle forebay, const char *symbol, const char *definition_id,
                              const char *label) {
    describe_function describer;
    const weivel_standard_description *description;
    weivel_standard_refusal refusal = WEIVEL_INIT(weivel_standard_refusal);
    if (symbol == NULL || definition_id == NULL) {
        fprintf(stderr, "%s declares no description of a forebay definition\n", label);
        check_failures++;
        return;
    }
    describer = describer_of(forebay, symbol);
    if (describer == NULL) {
        fprintf(stderr, "%s declares %s, which forebay-library does not export\n", label, symbol);
        check_failures++;
        return;
    }
    description = describer();
    if (weivel_standard_description_check(description, &refusal) != WEIVEL_STATUS_OK) {
        fprintf(stderr, "%s is refused at \"%s\": %s\n", symbol, refusal.path == NULL ? "" : refusal.path,
                refusal.detail == NULL ? "" : refusal.detail);
        weivel_standard_refusal_release(&refusal);
        check_failures++;
        return;
    }
    check_definition_name(description->definition, definition_id, label);
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
            check_description(forebay, library.descriptions[index], library.services[index].id,
                              library.services[index].id);
        }
        CHECK(library.contract_count > 0);
        for (size_t index = 0; index < library.contract_count; index++) {
            const weivel_standard_library_contract *contract = &library.contracts[index];
            check_description(forebay, contract->description, contract_definition_id(contract->capability),
                              contract->capability);
        }
        weivel_standard_library_release(&library);
    }
    unload_forebay(forebay);
}

int main(void) {
    every_declared_description_is_exported_and_checks();
    return check_summary("description_test");
}
