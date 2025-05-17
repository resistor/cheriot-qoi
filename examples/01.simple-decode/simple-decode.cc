#include <cdefs.h>
#include <compartment.h>
#include <compartment-macros.h>
#include <stdint.h>
#include <qoi_decode.h>
#include "../../third_party/display_drivers/lcd.hh"
#include <debug.hh>

using Debug = ConditionalDebug<true, "QOI Example">;

DECLARE_AND_DEFINE_QOI_DECODER(exampleDecoder)

uint8_t qoi_data[] = {
#embed "fpga.qoi"
};

void 
__cheri_compartment("simple_decode")
entry() {
    Debug::log("Starting simple_decode thread");
	static constexpr uint32_t ScreenWidth  = 160;
	static constexpr uint32_t ScreenHeight = 130;
    
    SonataLcd lcd;

    Debug::log("Initializing QOI decoder state");
    int r = qoi_decoder_state_init(STATIC_SEALED_VALUE(exampleDecoder));
    Debug::log("  Result: {}", r);

    Debug::log("Initializing QOI stream");
    qoi_stream stream;
    stream.in_buf = qoi_data;
    stream.in_buf_size = sizeof(qoi_data);

    uint32_t out_buf = 0;
    stream.out_buf = (unsigned char*)&out_buf;
    stream.out_buf_size = sizeof(out_buf);

    stream.decoder_state = STATIC_SEALED_VALUE(exampleDecoder);

    uint32_t x = 0;
    uint32_t y = 0;

    while (r = qoi_decode(&stream), r != QOI_STATUS_DONE) {
        lcd.draw_pixel({.x=x, .y=y}, Color(out_buf));
        out_buf = 0;
        stream.out_buf = (unsigned char*)&out_buf;
        stream.out_buf_size = sizeof(out_buf);
        x += 1;
        if (x == ScreenWidth) {
            x = 0;
            y += 1;
        }
    }

    while (true) {
        Timeout oneSecond{MS_TO_TICKS(500)};
        thread_sleep(&oneSecond, ThreadSleepFlags::ThreadSleepNoEarlyWake);
    }
}