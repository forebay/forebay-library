#ifndef FOREBAY_PUBLIC_DEFINITION_H
#define FOREBAY_PUBLIC_DEFINITION_H

/**
 * @file
 * How forebay-library declares the symbol of each service's definition, a `const void *f(void)` answering that
 * service's static struct.
 *
 * @implNote Exported only while the library itself is built (FOREBAY_LIBRARY_BUILDING), since MSVC refuses a
 * definition whose linkage differs from its declaration, and a test linking the objects statically would fail instead.
 */

#if defined(_WIN32) && defined(FOREBAY_LIBRARY_BUILDING)
#define FOREBAY_DEFINITION_API __declspec(dllexport)
#elif defined(_WIN32)
#define FOREBAY_DEFINITION_API
#else
#define FOREBAY_DEFINITION_API __attribute__((visibility("default")))
#endif

#endif
