#ifndef ANTHROPIC_KEYS_H
#define ANTHROPIC_KEYS_H

/** The extension keys the translator puts on an IR value for what the wire format has no member for. */
#define ANTHROPIC_EXTENSION_CONTENT_IS_STRING "$contentIsString"
#define ANTHROPIC_EXTENSION_SYSTEM_IS_STRING "$systemIsString"
#define ANTHROPIC_EXTENSION_THINKING_TYPE "$thinkingType"
#define ANTHROPIC_EXTENSION_CACHE_CONTROL_EXTRA "$cacheControlExtra"
#define ANTHROPIC_EXTENSION_IMAGE_SOURCE_RAW "$imageSourceRaw"
#define ANTHROPIC_EXTENSION_REDACTED "$redacted"
#define ANTHROPIC_EXTENSION_REDACTED_DATA "$redactedThinkingData"

#endif
