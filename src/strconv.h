#ifndef A0_SRC_STRCONV_H
#define A0_SRC_STRCONV_H

#include <a0/err.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Converts a uint64 to string.
// The entire buffer will be populated, if not with given the value, then with '0'.
// Populates the buffer from the back.
// start_ptr, if not null, will be set to the point within the buffer where the number starts.
// Does NOT check for overflow.
errno_t a0_u64_to_str(uint64_t val, char* buf_start, char* buf_end, char** start_ptr);

// Converts a string to uint64.
// The string may have leading '0's.
// Returns EINVAL if any character is not a digit.
// Does NOT check for overflow.
errno_t a0_str_to_u64(const char* start, const char* end, uint64_t* out);

#ifdef __cplusplus
}
#endif

#endif  // A0_SRC_STRCONV_H
