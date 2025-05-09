#define STBI_ONLY_PNG 1
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include "qoi_decode.h"

#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <cassert>
#include <cstdint>

int main(int argc, char** argv) {
    qoi_decoder_state decoder;
    qoi_decoder_state_init(&decoder);

    qoi_stream stream;

    int fd = open(argv[2], O_RDONLY);
    struct stat sb;
    fstat(fd, &sb);
    unsigned char* in_buf = (unsigned char*)mmap(NULL, sb.st_size, PROT_READ, MAP_PRIVATE, fd, 0);

    size_t in_idx = 0;
    size_t out_idx = 0;

    stream.in_buf = in_buf;
    stream.in_buf_size = 1;
    stream.out_buf = nullptr;
    stream.out_buf_size = 0;
    stream.decoder_state = &decoder;

    int r;

    // 14 bytes read for the header...
    for (int i = 0; i < 14; ++i) {
        r = qoi_decode(&stream);
        assert(r == QOI_STATUS_INPUT_EXHAUSTED);
        in_idx += 1;
        stream.in_buf = in_buf + in_idx;
        stream.in_buf_size = 1;
    }

    int x,y,n;
    unsigned char *data = stbi_load(argv[1], &x, &y, &n, stream.desc.channels);
    unsigned char* out_buf = (unsigned char*)calloc(x, y*stream.desc.channels);


    while (out_idx < x*y*n) {
        while (r = qoi_decode(&stream), r == QOI_STATUS_INPUT_EXHAUSTED) {
            in_idx += 1;
            stream.in_buf = in_buf + in_idx;
            stream.in_buf_size = 1;
        }

        assert(r == QOI_STATUS_OUTPUT_EXHAUSTED);
        while (r == QOI_STATUS_OUTPUT_EXHAUSTED) {
            assert(stream.in_buf_size == 0);
            stream.out_buf = out_buf + out_idx;
            stream.out_buf_size = 1;
            r = qoi_decode(&stream);
            assert(stream.out_buf_size == 0);
            assert(out_buf[out_idx] == data[out_idx]);
            out_idx += 1;
        }

        assert(r == QOI_STATUS_INPUT_EXHAUSTED);
        assert(stream.out_buf_size == 0);
        in_idx += 1;
        stream.in_buf = in_buf + in_idx;
        stream.in_buf_size = 1;
    }

    // 8 byte read for the trailer...
    assert(r == QOI_STATUS_INPUT_EXHAUSTED);
    for (int i = 0; i < 7; ++i) {
        r = qoi_decode(&stream);
        assert(r == QOI_STATUS_INPUT_EXHAUSTED);
        in_idx += 1;
        stream.in_buf = in_buf + in_idx;
        stream.in_buf_size = 1;
    }

    r = qoi_decode(&stream);
    assert(r == QOI_STATUS_DONE);

    return 0;
}
