#include <stdio.h>

#include "forebay/accounts.h"

/**
 * @file
 * Prints the layout of every public struct forebay-library's headers declare, as this compiler lays it out, so a
 * language that declares the same layouts by hand compares its own against the C compiler's rather than against a
 * guess. The format is weivel/standard's layouts program's:
 *
 *     <struct> <field> <offset> <size>
 *     <struct> sizeof <size>
 *
 * A struct's field lines come in declaration order and are followed by its sizeof line. An opaque struct has no
 * layout and is not listed. The spisor_ structs are spisor/library's, printed by its own layouts program.
 */

static void print_field(const char *type, const char *field, const void *instance, const void *member,
                        size_t size) {
    printf("%s %s %zu %zu\n", type, field, (size_t)((const char *)member - (const char *)instance), size);
}

static void print_size(const char *type, size_t size) {
    printf("%s sizeof %zu\n", type, size);
}

#define FIELD(type, member)                                                               \
    do {                                                                                  \
        static type instance;                                                             \
        print_field(#type, #member, &instance, &instance.member, sizeof instance.member); \
    } while (0)

#define SIZE(type) print_size(#type, sizeof(type))

static void print_accounts_layouts(void) {
    FIELD(forebay_accounts_list_argument, size);
    FIELD(forebay_accounts_list_argument, store);
    SIZE(forebay_accounts_list_argument);
    FIELD(forebay_accounts_list_answer, size);
    FIELD(forebay_accounts_list_answer, accounts);
    FIELD(forebay_accounts_list_answer, count);
    FIELD(forebay_accounts_list_answer, storage);
    SIZE(forebay_accounts_list_answer);
    FIELD(forebay_accounts_upsert_argument, size);
    FIELD(forebay_accounts_upsert_argument, store);
    FIELD(forebay_accounts_upsert_argument, account);
    SIZE(forebay_accounts_upsert_argument);
    FIELD(forebay_accounts_upsert_answer, size);
    FIELD(forebay_accounts_upsert_answer, outcome);
    SIZE(forebay_accounts_upsert_answer);
    FIELD(forebay_accounts_remove_argument, size);
    FIELD(forebay_accounts_remove_argument, store);
    FIELD(forebay_accounts_remove_argument, id);
    SIZE(forebay_accounts_remove_argument);
    FIELD(forebay_accounts_remove_answer, size);
    FIELD(forebay_accounts_remove_answer, removed);
    SIZE(forebay_accounts_remove_answer);
    FIELD(forebay_accounts_acquire_argument, size);
    FIELD(forebay_accounts_acquire_argument, store);
    FIELD(forebay_accounts_acquire_argument, lane);
    FIELD(forebay_accounts_acquire_argument, strategy);
    FIELD(forebay_accounts_acquire_argument, now);
    SIZE(forebay_accounts_acquire_argument);
    FIELD(forebay_accounts_acquire_answer, size);
    FIELD(forebay_accounts_acquire_answer, kind);
    FIELD(forebay_accounts_acquire_answer, id);
    FIELD(forebay_accounts_acquire_answer, available);
    FIELD(forebay_accounts_acquire_answer, knows_available_at);
    FIELD(forebay_accounts_acquire_answer, available_at);
    FIELD(forebay_accounts_acquire_answer, storage);
    SIZE(forebay_accounts_acquire_answer);
    FIELD(forebay_accounts_report_argument, size);
    FIELD(forebay_accounts_report_argument, store);
    FIELD(forebay_accounts_report_argument, id);
    FIELD(forebay_accounts_report_argument, outcome);
    FIELD(forebay_accounts_report_argument, now);
    FIELD(forebay_accounts_report_argument, lane);
    FIELD(forebay_accounts_report_argument, reason);
    FIELD(forebay_accounts_report_argument, has_reset_at);
    FIELD(forebay_accounts_report_argument, reset_at);
    FIELD(forebay_accounts_report_argument, has_random);
    FIELD(forebay_accounts_report_argument, random);
    FIELD(forebay_accounts_report_argument, attempt);
    FIELD(forebay_accounts_report_argument, base_milliseconds);
    FIELD(forebay_accounts_report_argument, max_milliseconds);
    FIELD(forebay_accounts_report_argument, quota);
    SIZE(forebay_accounts_report_argument);
    FIELD(forebay_accounts_report_answer, size);
    FIELD(forebay_accounts_report_answer, changed);
    SIZE(forebay_accounts_report_answer);
    FIELD(forebay_accounts_token_argument, size);
    FIELD(forebay_accounts_token_argument, has_expires);
    FIELD(forebay_accounts_token_argument, expires);
    FIELD(forebay_accounts_token_argument, now);
    FIELD(forebay_accounts_token_argument, has_skew_milliseconds);
    FIELD(forebay_accounts_token_argument, skew_milliseconds);
    SIZE(forebay_accounts_token_argument);
    FIELD(forebay_accounts_token_answer, size);
    FIELD(forebay_accounts_token_answer, expired);
    SIZE(forebay_accounts_token_answer);
    FIELD(forebay_accounts_capacity_argument, size);
    FIELD(forebay_accounts_capacity_argument, pools);
    FIELD(forebay_accounts_capacity_argument, count);
    SIZE(forebay_accounts_capacity_argument);
    FIELD(forebay_accounts_capacity_answer, size);
    FIELD(forebay_accounts_capacity_answer, capacity);
    SIZE(forebay_accounts_capacity_answer);
    FIELD(forebay_accounts_definition, size);
    FIELD(forebay_accounts_definition, context);
    FIELD(forebay_accounts_definition, list);
    FIELD(forebay_accounts_definition, upsert);
    FIELD(forebay_accounts_definition, remove);
    FIELD(forebay_accounts_definition, acquire);
    FIELD(forebay_accounts_definition, report);
    FIELD(forebay_accounts_definition, token_expired);
    FIELD(forebay_accounts_definition, has_capacity);
    FIELD(forebay_accounts_definition, release_list_answer);
    FIELD(forebay_accounts_definition, release_acquire_answer);
    FIELD(forebay_accounts_definition, release_refusal);
    SIZE(forebay_accounts_definition);
}

int main(void) {
    print_accounts_layouts();
    return 0;
}
