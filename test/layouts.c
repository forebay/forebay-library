#include <stdio.h>

#include "forebay/accounts.h"
#include "forebay/routing.h"

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

static void print_routing_layouts(void) {
    FIELD(forebay_pair, name);
    FIELD(forebay_pair, value);
    SIZE(forebay_pair);
    FIELD(forebay_assignment, provider);
    FIELD(forebay_assignment, model);
    FIELD(forebay_assignment, name);
    FIELD(forebay_assignment, derived);
    SIZE(forebay_assignment);
    FIELD(forebay_slot, slot);
    FIELD(forebay_slot, chain);
    FIELD(forebay_slot, count);
    SIZE(forebay_slot);
    FIELD(forebay_catalog_model, id);
    FIELD(forebay_catalog_model, name);
    SIZE(forebay_catalog_model);
    FIELD(forebay_catalog_provider, provider);
    FIELD(forebay_catalog_provider, models);
    FIELD(forebay_catalog_provider, model_count);
    FIELD(forebay_catalog_provider, has_ranking);
    FIELD(forebay_catalog_provider, ranking);
    FIELD(forebay_catalog_provider, ranking_count);
    SIZE(forebay_catalog_provider);
    FIELD(forebay_routing_profile, size);
    FIELD(forebay_routing_profile, tier_source_provider);
    FIELD(forebay_routing_profile, tier_order);
    FIELD(forebay_routing_profile, tier_order_count);
    FIELD(forebay_routing_profile, tier_fallback);
    FIELD(forebay_routing_profile, tier_fallback_count);
    FIELD(forebay_routing_profile, tier_regex);
    FIELD(forebay_routing_profile, native_model_pattern);
    FIELD(forebay_routing_profile, unserved_model_pattern);
    SIZE(forebay_routing_profile);
    FIELD(forebay_tiers_argument, size);
    FIELD(forebay_tiers_argument, profile);
    FIELD(forebay_tiers_argument, providers);
    FIELD(forebay_tiers_argument, count);
    SIZE(forebay_tiers_argument);
    FIELD(forebay_tiers_answer, size);
    FIELD(forebay_tiers_answer, tiers);
    FIELD(forebay_tiers_answer, count);
    FIELD(forebay_tiers_answer, storage);
    SIZE(forebay_tiers_answer);
    FIELD(forebay_model_map_argument, size);
    FIELD(forebay_model_map_argument, profile);
    FIELD(forebay_model_map_argument, settings);
    FIELD(forebay_model_map_argument, providers);
    FIELD(forebay_model_map_argument, provider_count);
    SIZE(forebay_model_map_argument);
    FIELD(forebay_model_map_answer, size);
    FIELD(forebay_model_map_answer, slots);
    FIELD(forebay_model_map_answer, slot_count);
    FIELD(forebay_model_map_answer, healed);
    FIELD(forebay_model_map_answer, healed_count);
    FIELD(forebay_model_map_answer, storage);
    SIZE(forebay_model_map_answer);
    FIELD(forebay_resolve_argument, size);
    FIELD(forebay_resolve_argument, profile);
    FIELD(forebay_resolve_argument, slots);
    FIELD(forebay_resolve_argument, slot_count);
    FIELD(forebay_resolve_argument, providers);
    FIELD(forebay_resolve_argument, provider_count);
    FIELD(forebay_resolve_argument, requested_model);
    SIZE(forebay_resolve_argument);
    FIELD(forebay_resolve_answer, size);
    FIELD(forebay_resolve_answer, chain);
    FIELD(forebay_resolve_answer, chain_count);
    FIELD(forebay_resolve_answer, notices);
    FIELD(forebay_resolve_answer, notice_count);
    FIELD(forebay_resolve_answer, storage);
    SIZE(forebay_resolve_answer);
    FIELD(forebay_outcome_argument, size);
    FIELD(forebay_outcome_argument, status);
    FIELD(forebay_outcome_argument, headers);
    FIELD(forebay_outcome_argument, header_count);
    FIELD(forebay_outcome_argument, now);
    FIELD(forebay_outcome_argument, has_retry_after);
    FIELD(forebay_outcome_argument, retry_after_milliseconds);
    SIZE(forebay_outcome_argument);
    FIELD(forebay_outcome_answer, size);
    FIELD(forebay_outcome_answer, rate_limited);
    FIELD(forebay_outcome_answer, has_reset_at);
    FIELD(forebay_outcome_answer, reset_at);
    SIZE(forebay_outcome_answer);
    FIELD(forebay_report, size);
    FIELD(forebay_report, kind);
    FIELD(forebay_report, after_bytes);
    FIELD(forebay_report, message);
    FIELD(forebay_report, status);
    FIELD(forebay_report, headers);
    FIELD(forebay_report, header_count);
    FIELD(forebay_report, has_retry_after);
    FIELD(forebay_report, retry_after_milliseconds);
    FIELD(forebay_report, now);
    SIZE(forebay_report);
    FIELD(forebay_step_argument, size);
    FIELD(forebay_step_argument, providers);
    FIELD(forebay_step_argument, provider_count);
    FIELD(forebay_step_argument, walk);
    FIELD(forebay_step_argument, profile);
    FIELD(forebay_step_argument, slots);
    FIELD(forebay_step_argument, slot_count);
    FIELD(forebay_step_argument, catalog);
    FIELD(forebay_step_argument, catalog_count);
    FIELD(forebay_step_argument, model);
    FIELD(forebay_step_argument, pass_through);
    FIELD(forebay_step_argument, refreshed);
    FIELD(forebay_step_argument, login_command);
    FIELD(forebay_step_argument, settings_file);
    FIELD(forebay_step_argument, report);
    SIZE(forebay_step_argument);
    FIELD(forebay_log_line, level);
    FIELD(forebay_log_line, message);
    SIZE(forebay_log_line);
    FIELD(forebay_step_answer, size);
    FIELD(forebay_step_answer, kind);
    FIELD(forebay_step_answer, provider);
    FIELD(forebay_step_answer, model);
    FIELD(forebay_step_answer, reply_kind);
    FIELD(forebay_step_answer, status);
    FIELD(forebay_step_answer, message);
    FIELD(forebay_step_answer, headers);
    FIELD(forebay_step_answer, header_count);
    FIELD(forebay_step_answer, has_reset_at);
    FIELD(forebay_step_answer, reset_at);
    FIELD(forebay_step_answer, error_type);
    FIELD(forebay_step_answer, log);
    FIELD(forebay_step_answer, log_count);
    FIELD(forebay_step_answer, walk);
    FIELD(forebay_step_answer, storage);
    SIZE(forebay_step_answer);
    FIELD(forebay_limits_argument, size);
    SIZE(forebay_limits_argument);
    FIELD(forebay_limits_answer, size);
    FIELD(forebay_limits_answer, max_request_bytes);
    FIELD(forebay_limits_answer, too_large_status);
    FIELD(forebay_limits_answer, too_large_message);
    FIELD(forebay_limits_answer, models_budget_milliseconds);
    FIELD(forebay_limits_answer, rederive_interval_milliseconds);
    FIELD(forebay_limits_answer, refresh_every_milliseconds);
    SIZE(forebay_limits_answer);
    FIELD(forebay_pass_through_argument, size);
    FIELD(forebay_pass_through_argument, upstream);
    FIELD(forebay_pass_through_argument, url);
    FIELD(forebay_pass_through_argument, headers);
    FIELD(forebay_pass_through_argument, header_count);
    FIELD(forebay_pass_through_argument, body_length);
    FIELD(forebay_pass_through_argument, placeholder_authorization);
    SIZE(forebay_pass_through_argument);
    FIELD(forebay_pass_through_answer, size);
    FIELD(forebay_pass_through_answer, target);
    FIELD(forebay_pass_through_answer, headers);
    FIELD(forebay_pass_through_answer, header_count);
    FIELD(forebay_pass_through_answer, storage);
    SIZE(forebay_pass_through_answer);
    FIELD(forebay_reply_headers_argument, size);
    FIELD(forebay_reply_headers_argument, headers);
    FIELD(forebay_reply_headers_argument, header_count);
    FIELD(forebay_reply_headers_argument, rewritten);
    SIZE(forebay_reply_headers_argument);
    FIELD(forebay_reply_headers_answer, size);
    FIELD(forebay_reply_headers_answer, headers);
    FIELD(forebay_reply_headers_answer, header_count);
    FIELD(forebay_reply_headers_answer, storage);
    SIZE(forebay_reply_headers_answer);
    FIELD(forebay_arrival_argument, size);
    FIELD(forebay_arrival_argument, path);
    FIELD(forebay_arrival_argument, headers);
    FIELD(forebay_arrival_argument, header_count);
    FIELD(forebay_arrival_argument, port);
    SIZE(forebay_arrival_argument);
    FIELD(forebay_arrival_answer, size);
    FIELD(forebay_arrival_answer, kind);
    FIELD(forebay_arrival_answer, status);
    FIELD(forebay_arrival_answer, content_type);
    FIELD(forebay_arrival_answer, body);
    FIELD(forebay_arrival_answer, reply_kind);
    FIELD(forebay_arrival_answer, message);
    FIELD(forebay_arrival_answer, storage);
    SIZE(forebay_arrival_answer);
    FIELD(forebay_port_failure, size);
    FIELD(forebay_port_failure, in_use);
    FIELD(forebay_port_failure, detail);
    SIZE(forebay_port_failure);
    FIELD(forebay_port_argument, size);
    FIELD(forebay_port_argument, value);
    FIELD(forebay_port_argument, settings_file);
    FIELD(forebay_port_argument, failure);
    SIZE(forebay_port_argument);
    FIELD(forebay_port_answer, size);
    FIELD(forebay_port_answer, port);
    FIELD(forebay_port_answer, address);
    FIELD(forebay_port_answer, storage);
    SIZE(forebay_port_answer);
    FIELD(forebay_failure_argument, size);
    FIELD(forebay_failure_argument, reason);
    FIELD(forebay_failure_argument, upstream);
    FIELD(forebay_failure_argument, detail);
    SIZE(forebay_failure_argument);
    FIELD(forebay_failure_answer, size);
    FIELD(forebay_failure_answer, reply_kind);
    FIELD(forebay_failure_answer, status);
    FIELD(forebay_failure_answer, message);
    FIELD(forebay_failure_answer, log);
    FIELD(forebay_failure_answer, log_count);
    FIELD(forebay_failure_answer, storage);
    SIZE(forebay_failure_answer);
    FIELD(forebay_state_file, size);
    FIELD(forebay_state_file, directory_variable);
    FIELD(forebay_state_file, name);
    FIELD(forebay_state_file, member);
    SIZE(forebay_state_file);
    FIELD(forebay_application, size);
    FIELD(forebay_application, base_url_variable);
    FIELD(forebay_application, placeholder_variable);
    FIELD(forebay_application, placeholder_value);
    FIELD(forebay_application, own_login_variables);
    FIELD(forebay_application, own_login_variable_count);
    FIELD(forebay_application, state_file);
    FIELD(forebay_application, tier_model_variable);
    FIELD(forebay_application, tier_model_name_variable);
    FIELD(forebay_application, model_variable);
    SIZE(forebay_application);
    FIELD(forebay_environment_argument, size);
    FIELD(forebay_environment_argument, slots);
    FIELD(forebay_environment_argument, slot_count);
    FIELD(forebay_environment_argument, port);
    FIELD(forebay_environment_argument, providers_enabled);
    FIELD(forebay_environment_argument, environment);
    FIELD(forebay_environment_argument, variable_count);
    FIELD(forebay_environment_argument, user_home);
    FIELD(forebay_environment_argument, application);
    SIZE(forebay_environment_argument);
    FIELD(forebay_environment_answer, size);
    FIELD(forebay_environment_answer, environment);
    FIELD(forebay_environment_answer, variable_count);
    FIELD(forebay_environment_answer, storage);
    SIZE(forebay_environment_answer);
    FIELD(forebay_routing_definition, size);
    FIELD(forebay_routing_definition, context);
    FIELD(forebay_routing_definition, tiers);
    FIELD(forebay_routing_definition, model_map);
    FIELD(forebay_routing_definition, resolve);
    FIELD(forebay_routing_definition, outcome);
    FIELD(forebay_routing_definition, step);
    FIELD(forebay_routing_definition, limits);
    FIELD(forebay_routing_definition, pass_through);
    FIELD(forebay_routing_definition, reply_headers);
    FIELD(forebay_routing_definition, arrival);
    FIELD(forebay_routing_definition, port);
    FIELD(forebay_routing_definition, failure);
    FIELD(forebay_routing_definition, environment);
    FIELD(forebay_routing_definition, release_tiers_answer);
    FIELD(forebay_routing_definition, release_model_map_answer);
    FIELD(forebay_routing_definition, release_resolve_answer);
    FIELD(forebay_routing_definition, release_step_answer);
    FIELD(forebay_routing_definition, release_arrival_answer);
    FIELD(forebay_routing_definition, release_walk);
    FIELD(forebay_routing_definition, release_pass_through_answer);
    FIELD(forebay_routing_definition, release_reply_headers_answer);
    FIELD(forebay_routing_definition, release_port_answer);
    FIELD(forebay_routing_definition, release_failure_answer);
    FIELD(forebay_routing_definition, release_environment_answer);
    FIELD(forebay_routing_definition, release_refusal);
    SIZE(forebay_routing_definition);
}

int main(void) {
    print_accounts_layouts();
    print_routing_layouts();
    return 0;
}
