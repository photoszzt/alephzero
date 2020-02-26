#ifndef A0_PACKET_H
#define A0_PACKET_H

#include <a0/alloc.h>
#include <a0/common.h>

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

// A packet is a simple data format that contains key-value headers and a payload.
//
// The header fields may be arbitrary byte arrays and key can be repeated.
//
// Header fields whose keys start with "a0_" are special cased.
// Among them are:
//   * "a0_id": The unique id of the packet.
//              It may NOT be provided! It will be auto-generated by the build function.
//   * "a0_deps": A repeated key referencing the unique ids of other packets.
//
// A packet is implemented as a flat buffer. The layout is described below.

typedef struct a0_packet_header_s {
  const char* key;
  const char* val;
} a0_packet_header_t;

// A headers block contains a list of headers, along with an optional pointer to the next block.
// https://en.wikipedia.org/wiki/Unrolled_linked_list
typedef struct a0_packet_headers_block_s a0_packet_headers_block_t;

struct a0_packet_headers_block_s {
  a0_packet_header_t* headers;
  size_t size;
  a0_packet_headers_block_t* next_block;
};

// Packet ids are human-readable uuidv4.
#define A0_PACKET_ID_SIZE 37
typedef char a0_packet_id_t[A0_PACKET_ID_SIZE];

typedef struct a0_packet_s {
  a0_packet_id_t id;
  a0_packet_headers_block_t headers_block;
  a0_buf_t payload;
} a0_packet_t;

// The following are special keys.
// The returned buffers should not be cleaned up.

const char* a0_packet_dep_key();

// Callback definition where packet is the only argument.

typedef struct a0_packet_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_packet_t);
} a0_packet_callback_t;

typedef struct a0_packet_header_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_packet_header_t);
} a0_packet_header_callback_t;

typedef struct a0_packet_id_callback_s {
  void* user_data;
  void (*fn)(void* user_data, a0_packet_id_t);
} a0_packet_id_callback_t;

errno_t a0_packet_init(a0_packet_t*);

typedef struct a0_packet_stats_s {
  size_t num_hdrs;
  size_t content_size;
  size_t serial_size;
} a0_packet_stats_t;

// Compute packet statistics.
errno_t a0_packet_stats(const a0_packet_t, a0_packet_stats_t*);

// Executes the given callback on all headers.
// This includes headers across blocks.
errno_t a0_packet_for_each_header(const a0_packet_headers_block_t, a0_packet_header_callback_t);

// Serializes the packet to the allocated location.
// Note: the header order will NOT be retained.
errno_t a0_packet_serialize(const a0_packet_t, a0_alloc_t, a0_buf_t* out);

// Deserializes the buffer into a packet.
// The alloc is only used for the header pointers, not the contents.
// The content will point into the buffer.
errno_t a0_packet_deserialize(const a0_buf_t, a0_alloc_t, a0_packet_t* out);

// Deep copies the packet contents.
errno_t a0_packet_deep_copy(const a0_packet_t, a0_alloc_t, a0_packet_t* out);

// The format of a packet is described here.
// It is recommended to not worry about this too much, and just use a0_packet_build.
//
// A packet has a header region followed by a payload.
// The header has a lookup table followed by a number of key-value pairs.
// The lookup table is designed for O(1) lookup of headers and the payload.
//
// +-------------------------------+
// | id (a0_packet_id_t)           |
// +-------------------------------+
// | num headers (size_t)          |
// +-------------------------------+
// | offset for hdr 0 key (size_t) |
// +-------------------------------+
// | offset for hdr 0 val (size_t) |
// +-------------------------------+
// |                               |
// .   .   .   .   .   .   .   .   .
// .   .   .   .   .   .   .   .   .
// .   .   .   .   .   .   .   .   .
// |                               |
// +-------------------------------+
// | offset for hdr N key (size_t) |
// +-------------------------------+
// | offset for hdr N val (size_t) |
// +-------------------------------+
// | offset for payload (size_t)   |
// +-------------------------------+
// | hdr 0 key content             |
// +-------------------------------+
// | hdr 0 val content             |
// +-------------------------------+
// |                               |
// .   .   .   .   .   .   .   .   .
// .   .   .   .   .   .   .   .   .
// .   .   .   .   .   .   .   .   .
// +-------------------------------+
// | hdr N key content             |
// +-------------------------------+
// | hdr N val content             |
// +-------------------------------+
// | payload content               |
// +-------------------------------+

#ifdef __cplusplus
}
#endif

#endif  // A0_PACKET_H
