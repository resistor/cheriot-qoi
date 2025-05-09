#include <cdefs.h>
#include <compartment.h>
#include <compartment-macros.h>

#include <qoi_decode.h>

DECLARE_AND_DEFINE_QOI_DECODER(exampleDecoder)

void 
__cheri_compartment("simple_decode")
entry() {
    qoi_decoder_state_init(STATIC_SEALED_VALUE(exampleDecoder));
    return;
}