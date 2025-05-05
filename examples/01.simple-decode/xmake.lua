-- Update this to point to the location of the CHERIoT SDK
sdkdir = path.absolute("../../cheriot-rtos/sdk")
set_project("CHERIoT Simple QOI Decode Example")

includes(sdkdir)

set_toolchains("cheriot-clang")