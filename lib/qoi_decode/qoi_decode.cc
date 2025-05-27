#include <qoi_decode.h>

#ifdef __CHERIOT__
#include <token.h>
#include <cheri.hh>
#include <debug.hh>
using Debug = ConditionalDebug<true, "QOI Decoder">;
#endif

static constexpr size_t QOI_PIXELS_MAX = 400000000;

static constexpr uint8_t qoi_magic[4] = {'q', 'o', 'i', 'f'};
static_assert(__BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__);

#ifdef __CHERIOT__
static qoi_decoder_state *qoi_unseal(
    qoi_decoder_state *__sealed_capability sealed) {
  return token_unseal(STATIC_SEALING_TYPE(QOIDecoderStateKey), sealed);
}
#else
// Provide a no-op implementation of unsealing when building for
// non-CHERIoT.
static qoi_decoder_state *qoi_unseal(
    qoi_decoder_state *__sealed_capability sealed) {
  return sealed;
}
#endif

static int qoi_progress_await_magic(qoi_decoder_state *, qoi_stream *);
static int qoi_progress_await_width(qoi_decoder_state *, qoi_stream *);
static int qoi_progress_await_height(qoi_decoder_state *, qoi_stream *);
static int qoi_progress_await_channels(qoi_decoder_state *, qoi_stream *);
static int qoi_progress_await_colorspace(qoi_decoder_state *, qoi_stream *);
static int qoi_progress_new_pixel(qoi_decoder_state *, qoi_stream *);
static int qoi_progress_op_rgba(qoi_decoder_state *, qoi_stream *);
static int qoi_progress_buffered_output(qoi_decoder_state *, qoi_stream *);
static int qoi_progress_await_tail(qoi_decoder_state *, qoi_stream *);

int qoi_decoder_state_init(
    qoi_decoder_state *__sealed_capability sealed_decoder) {
  auto *decoder = qoi_unseal(sealed_decoder);
  if (!decoder) return QOI_STATUS_ERR_PARAM;
  *decoder = {
      .next_step = &qoi_progress_await_magic,
      .px_prev = 0xFF000000,
      .tmp_buf = {.v = {}},
  };
  return 0;
}

// Shift bytes from the input buffer into the decoder's internal buffer.
static void qoi_shift_bytes(qoi_decoder_state *decoder, qoi_stream *stream,
                            size_t bytes) {
  if (decoder->tmp_buf_size >= bytes) return;

  size_t required = bytes - decoder->tmp_buf_size;
  size_t count =
      (stream->in_buf_size > required) ? required : stream->in_buf_size;
  memcpy(decoder->tmp_buf.b + decoder->tmp_buf_size, stream->in_buf, count);
  decoder->tmp_buf_size += count;
  stream->in_buf += count;
  stream->in_buf_size -= count;
}

// Shared helper for writing a pixel, including updating the `index` array.
static int qoi_output_pixel(qoi_decoder_state *decoder, qoi_stream *stream,
                            uint32_t pixel) {
  decoder->tmp_buf.v = pixel;
  decoder->tmp_buf_size = stream->desc.channels;
  decoder->px_prev = pixel;

  uint8_t pixel_channels[4];
  memcpy(pixel_channels, &pixel, 4);
  size_t pixel_idx = pixel_channels[0] * 3 + pixel_channels[1] * 5 +
                     pixel_channels[2] * 7 + pixel_channels[3] * 11;
  pixel_idx %= 64;
  decoder->index[pixel_idx] = pixel;

  return qoi_progress_buffered_output(decoder, stream);
}

#define TMP_BUF_RESET()   \
  decoder->tmp_buf.v = 0; \
  decoder->tmp_buf_size = 0;

#define VERIFY_TMP_BUF_RESET()                                                 \
  if (decoder->tmp_buf_size > 0 || decoder->tmp_buf.v != 0) {                  \
    decoder->next_step = &qoi_progress_invalid;                                \
    return QOI_STATUS_ERR_INTERNAL;                                            \
  }

static int qoi_progress_invalid(qoi_decoder_state *, qoi_stream *) {
  // Once the decoder is in an invalid state, it never exists it
  // until it is re-initialized.
  return QOI_STATUS_ERR_PARAM;
}

static int qoi_progress_await_magic(qoi_decoder_state *decoder,
                                    qoi_stream *stream) {
  decoder->next_step = &qoi_progress_await_magic;

  // Buffer 4 bytes to hold the magic constant.
  constexpr size_t MAGIC_SIZE = sizeof(qoi_magic);
  static_assert(MAGIC_SIZE <= sizeof(decoder->tmp_buf));
  qoi_shift_bytes(decoder, stream, MAGIC_SIZE);
  if (decoder->tmp_buf_size < MAGIC_SIZE) return QOI_STATUS_INPUT_EXHAUSTED;

  // Verify the magic constant.
  if (memcmp(&decoder->tmp_buf, qoi_magic, MAGIC_SIZE)) {
    decoder->next_step = &qoi_progress_invalid;
    return QOI_STATUS_ERR_FORMAT;
  }

  TMP_BUF_RESET();

  return qoi_progress_await_width(decoder, stream);
}

static int qoi_progress_await_width(qoi_decoder_state *decoder,
                                    qoi_stream *stream) {
  decoder->next_step = &qoi_progress_await_width;

  // Buffer 4 bytes for the width.
  constexpr size_t FIELD_SIZE = sizeof(stream->desc.width);
  static_assert(FIELD_SIZE <= sizeof(decoder->tmp_buf));
  qoi_shift_bytes(decoder, stream, FIELD_SIZE);
  if (decoder->tmp_buf_size < FIELD_SIZE) return QOI_STATUS_INPUT_EXHAUSTED;

  // Read the width.
  stream->desc.width = __builtin_bswap32(decoder->tmp_buf.v);

  TMP_BUF_RESET();

  return qoi_progress_await_height(decoder, stream);
}

static int qoi_progress_await_height(qoi_decoder_state *decoder,
                                     qoi_stream *stream) {
  decoder->next_step = qoi_progress_await_height;

  // Buffer 4 bytes for the height.
  constexpr size_t FIELD_SIZE = sizeof(stream->desc.height);
  static_assert(FIELD_SIZE <= sizeof(decoder->tmp_buf));
  qoi_shift_bytes(decoder, stream, FIELD_SIZE);
  if (decoder->tmp_buf_size < FIELD_SIZE) return QOI_STATUS_INPUT_EXHAUSTED;

  // Read the height.
  stream->desc.height = __builtin_bswap32(decoder->tmp_buf.v);
  if (stream->desc.height >= QOI_PIXELS_MAX / stream->desc.width) {
    decoder->next_step = qoi_progress_invalid;
    return QOI_STATUS_ERR_FORMAT;
  }
  decoder->pixel_length_remaining = stream->desc.width * stream->desc.height;

  TMP_BUF_RESET();

  return qoi_progress_await_channels(decoder, stream);
}

static int qoi_progress_await_channels(qoi_decoder_state *decoder,
                                       qoi_stream *stream) {
  decoder->next_step = qoi_progress_await_channels;

  // We don't use the internal buffer here, so verify that
  // it's empty.
  VERIFY_TMP_BUF_RESET();

  // Check that the input is ready.
  if (stream->in_buf_size < 1) return QOI_STATUS_INPUT_EXHAUSTED;

  // Read the `channel` and sanity check it.
  uint8_t channels = stream->in_buf[0];
  if (channels != 3 && channels != 4) {
    decoder->next_step = &qoi_progress_invalid;
    return QOI_STATUS_ERR_FORMAT;
  }
  stream->desc.channels = channels;

  stream->in_buf += 1;
  stream->in_buf_size -= 1;

  return qoi_progress_await_colorspace(decoder, stream);
}

static int qoi_progress_await_colorspace(qoi_decoder_state *decoder,
                                         qoi_stream *stream) {
  decoder->next_step = &qoi_progress_await_colorspace;

  // We don't use the internal buffer here, so verify that
  // it's empty.
  VERIFY_TMP_BUF_RESET();

  // Check that the input is ready.
  if (stream->in_buf_size < 1) return QOI_STATUS_INPUT_EXHAUSTED;

  // Read the `colorspace` and sanity check it.
  uint8_t colorspace = stream->in_buf[0];
  if (colorspace != 0 && colorspace != 4) {
    decoder->next_step = qoi_progress_invalid;
    return QOI_STATUS_ERR_FORMAT;
  }
  stream->desc.colorspace = colorspace;

  stream->in_buf += 1;
  stream->in_buf_size -= 1;

  return qoi_progress_new_pixel(decoder, stream);
}

static int qoi_progress_new_pixel(qoi_decoder_state *decoder,
                                  qoi_stream *stream) {
  decoder->next_step = &qoi_progress_new_pixel;

  // Before decoding a new command from the input, first check for
  // any pending `QOI_OP_RUN` commands. These must be drained before
  // we read any more input.
  if (decoder->pending_run_count > 0) {
    decoder->pending_run_count -= 1;
    return qoi_output_pixel(decoder, stream, decoder->px_prev);
  }

  // Buffer the first byte of the next command.
  qoi_shift_bytes(decoder, stream, 1);
  if (decoder->tmp_buf_size < 1) return QOI_STATUS_INPUT_EXHAUSTED;

  uint8_t byte0 = decoder->tmp_buf.b[0];

  // Dispatch based on the first byte.
  if (byte0 == 0b11111110) {
    // QOI_OP_RGB
    qoi_shift_bytes(decoder, stream, 4);
    if (decoder->tmp_buf_size < 4) return QOI_STATUS_INPUT_EXHAUSTED;

    uint32_t pixel = decoder->tmp_buf.v >> 8;
    pixel |= decoder->px_prev & 0xFF000000;
    return qoi_output_pixel(decoder, stream, pixel);
  } else if (byte0 == 0b11111111) {
    // QOI_OP_RGBA
    decoder->tmp_buf.v = 0;
    decoder->tmp_buf_size = 0;
    return qoi_progress_op_rgba(decoder, stream);
  }

  uint8_t byte0_decode = byte0 & 0b11000000;
  switch (byte0_decode) {
    case 0b00000000: {
      // QOI_OP_INDEX
      uint8_t idx = byte0 & 0b111111;
      uint32_t pixel = decoder->index[idx];
      return qoi_output_pixel(decoder, stream, pixel);
    }
    case 0b01000000: {
      // QOI_OP_DIFF
      uint8_t channels[4];
      memcpy(channels, &decoder->px_prev, 4);
      uint8_t dr = (byte0 & 0b00110000) >> 4;
      uint8_t dg = (byte0 & 0b00001100) >> 2;
      uint8_t db = (byte0 & 0b00000011) >> 0;
      channels[0] += dr - 2;
      channels[1] += dg - 2;
      channels[2] += db - 2;
      uint32_t pixel;
      memcpy(&pixel, channels, 4);
      return qoi_output_pixel(decoder, stream, pixel);
    }
    case 0b10000000: {
      // QOI_OP_LUMA
      qoi_shift_bytes(decoder, stream, 2);
      if (decoder->tmp_buf_size < 2) return QOI_STATUS_INPUT_EXHAUSTED;

      uint8_t channels[4];
      memcpy(channels, &decoder->px_prev, 4);
      uint8_t dg = (byte0 & 0x3F) - 32;
      uint8_t drdg = ((decoder->tmp_buf.b[1] & 0b11110000) >> 4) - 8;
      uint8_t dbdg = ((decoder->tmp_buf.b[1] & 0b00001111) >> 0) - 8;
      channels[1] += dg;
      channels[0] += drdg + dg;
      channels[2] += dbdg + dg;
      uint32_t pixel;
      memcpy(&pixel, channels, 4);
      return qoi_output_pixel(decoder, stream, pixel);
    }
    case 0b11000000: {
      // QOI_OP_RUN
      decoder->pending_run_count = (byte0 & 0b111111) + 1;
      return qoi_progress_new_pixel(decoder, stream);
    }
  }

  decoder->next_step = &qoi_progress_invalid;
  return QOI_STATUS_ERR_INTERNAL;
}

// QOI_OP_RGBA is needs its own QOI_PROGRESS_* state because
// the command occupies 5 bytes in the input stream, which
// doesn't fit in the temporary buffer. By using the op-
// specific state to record that that's already decoded the
// first byte, we then only need to buffer the 4 remaining
// bytes here.
static int qoi_progress_op_rgba(qoi_decoder_state *decoder,
                                qoi_stream *stream) {
  decoder->next_step = &qoi_progress_op_rgba;

  qoi_shift_bytes(decoder, stream, 4);
  if (decoder->tmp_buf_size < 4) return QOI_STATUS_INPUT_EXHAUSTED;

  uint32_t pixel = decoder->tmp_buf.v;
  return qoi_output_pixel(decoder, stream, pixel);
}

static int qoi_progress_buffered_output(qoi_decoder_state *decoder,
                                        qoi_stream *stream) {
  decoder->next_step = &qoi_progress_buffered_output;

  if (stream->out_buf_size == 0) return QOI_STATUS_OUTPUT_EXHAUSTED;

  // Output as many bytes as we have space for in the output buffer.
  size_t count = (decoder->tmp_buf_size > stream->out_buf_size)
                     ? stream->out_buf_size
                     : decoder->tmp_buf_size;
  memcpy(stream->out_buf, &decoder->tmp_buf, count);
  decoder->tmp_buf.v >>= 8 * count;
  decoder->tmp_buf_size -= count;
  stream->out_buf += count;
  stream->out_buf_size -= count;

  if (decoder->tmp_buf_size > 0) return QOI_STATUS_OUTPUT_EXHAUSTED;

  // Only mark the pixel as complete after we've drained the temp buffer.
  decoder->pixel_length_remaining -= 1;
  TMP_BUF_RESET();

  if (decoder->pixel_length_remaining > 0) {
    return qoi_progress_new_pixel(decoder, stream);
  } else {
    return qoi_progress_await_tail(decoder, stream);
  }
}

static int qoi_progress_await_tail(qoi_decoder_state *decoder,
                                   qoi_stream *stream) {
  decoder->next_step = &qoi_progress_await_tail;

  if (stream->in_buf_size == 0) return QOI_STATUS_INPUT_EXHAUSTED;

  // Verify the tail padding.
  if (decoder->tmp_buf.v < 7) {
    if (stream->in_buf[0] == 0) {
      decoder->tmp_buf.v += 1;
      stream->in_buf += 1;
      stream->in_buf_size -= 1;
      return qoi_progress_await_tail(decoder, stream);
    }
  } else if (decoder->tmp_buf.v == 7) {
    if (stream->in_buf[0] == 1) {
      decoder->next_step = &qoi_progress_invalid;
      return QOI_STATUS_DONE;
    }
  }

  decoder->next_step = &qoi_progress_invalid;
  return QOI_STATUS_ERR_FORMAT;
}

int qoi_decode(qoi_stream *stream) {
#if __CHERIOT__
  if (!CHERI::check_pointer<CHERI::PermissionSet{
          CHERI::Permission::Load, CHERI::Permission::Store,
          CHERI::Permission::LoadMutable,
          CHERI::Permission::LoadStoreCapability}>(stream, sizeof(qoi_stream)))
    return QOI_STATUS_ERR_PARAM;

  if (stream->in_buf_size > 0 &&
      !CHERI::check_pointer<CHERI::PermissionSet{CHERI::Permission::Store}>(
          stream->in_buf, stream->in_buf_size))
    return QOI_STATUS_ERR_PARAM;

  if (stream->out_buf_size > 0 &&
      !CHERI::check_pointer<CHERI::PermissionSet{CHERI::Permission::Store}>(
          stream->out_buf, stream->out_buf_size))
    return QOI_STATUS_ERR_PARAM;
#endif

  qoi_decoder_state *decoder = qoi_unseal(stream->decoder_state);
  if (!decoder) return QOI_STATUS_ERR_PARAM;

#if __CHERIOT__
  if (!CHERI::check_pointer<CHERI::PermissionSet{
                                CHERI::Permission::Load,
                                CHERI::Permission::Store},
                            true, true>(decoder, sizeof(qoi_decoder_state)))
    return QOI_STATUS_ERR_PARAM;
#endif

  // Dispatch based on the current progress.
  return decoder->next_step(decoder, stream);
}
