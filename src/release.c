#include "service_support.h"

SERVICE_EXPORT void forebay_library_release(uint8_t *buffer) {
    spisor_release(buffer);
}
