#pragma once

#include <stdint.h>

#ifdef __CHERIOT__
#include <cdefs.h>
#include <compartment.h>
#include <compartment-macros.h>
#else
#include <string.h>

// Defined away some CHERIoT macros when building for host.
#define DECLARE_AND_DEFINE_STATIC_SEALED_VALUE(a,b,c,d,e,f)
#define __cheri_compartment(a)
#define __sealed_capability
#define __DECL
#endif

// Parsed values from the header of the QOI file.
typedef struct {
	unsigned int width;
	unsigned int height;
	unsigned char channels;
	unsigned char colorspace;
} qoi_desc;

// Private internal decoder state.
typedef struct {
  unsigned char progress;
  size_t pixel_length_remaining;
  uint32_t px_prev;
  uint32_t index[64];
  union {
    uint32_t v;
    uint8_t b[4];
  } tmp_buf;
  unsigned char tmp_buf_size;
  unsigned char pending_run_count;
} qoi_decoder_state;

// Can be used to statically define a `qoi_decoder_state` in the
// calling compartment.
#define DECLARE_AND_DEFINE_QOI_DECODER(name)                   \
	DECLARE_AND_DEFINE_STATIC_SEALED_VALUE(                  \
        qoi_decoder_state, qoi_decode, QOIDecoderStateKey, name, {})

typedef struct {
  // Points to the next byte of input to be consumed.
  const unsigned char* in_buf;
  // Number of bytes of input remaining in the buffer.
  size_t in_buf_size;

  // Points to the next byte of output to be written.
  unsigned char* out_buf;
  // Number of bytes of output space remaining.
  size_t out_buf_size;

  // Parsed QOI file header.
  qoi_desc desc;

  // Private internal decoder state.
  qoi_decoder_state * __sealed_capability decoder_state;
} qoi_stream;

// Initializes (or resets) a `qoi_decoder_state`.
__DECL int __cheri_compartment("qoi_decode")
    qoi_decoder_state_init(qoi_decoder_state* __sealed_capability);

// Return values of `qoi_decode`
#define QOI_STATUS_ERR_INTERNAL -4
#define QOI_STATUS_ERR_PARAM -3
#define QOI_STATUS_ERR_FORMAT -2
#define QOI_STATUS_DONE 0
#define QOI_STATUS_INPUT_EXHAUSTED 1
#define QOI_STATUS_OUTPUT_EXHAUSTED 2

// Decodes QOI-formatted data from the given stream.
__DECL int __cheri_compartment("qoi_decode") qoi_decode(qoi_stream*);
