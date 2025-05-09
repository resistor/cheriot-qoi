-- Update this to point to the location of the CHERIoT SDK
sdkdir = path.absolute("../../cheriot-rtos/sdk")
set_project("CHERIoT Simple QOI Decode Example")

includes(sdkdir)

set_toolchains("cheriot-clang")

includes(path.join(sdkdir, "lib"))
includes("../../lib")

option("board")
  set_default("sail")

compartment("simple_decode")
    add_includedirs("../../include")
    add_deps("freestanding", "qoi_decode")
    add_files("simple-decode.c")

firmware("01.simple_decode")
    add_deps("simple_decode")
    on_load(function(target)
        target:values_set("board", "$(board)")
        target:values_set("threads", {
          {
            compartment = "simple_decode",
            priority = 1,
            entry_point = "entry",
            stack_size = 0xe00,
            trusted_stack_frames = 6
          },
        }, {expand = false})
    end)