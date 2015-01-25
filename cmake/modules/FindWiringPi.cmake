# Try to find the wiringPi library.
# This will define:
#
# WiringPi_FOUND       - wiringPi library is available
# WiringPi_INCLUDE_DIR - Where the wiringPi.h header file is
# WiringPi_LIBRARIES   - The libraries to link in.

find_path(WiringPi_INCLUDE_DIR wiringPi.h)
find_library(WiringPi_LIBRARY NAMES wiringpi wiringPi)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(WiringPi DEFAULT_MSG WiringPi_INCLUDE_DIR WiringPi_LIBRARY)
set(WiringPi_LIBRARIES ${WiringPi_LIBRARY})
mark_as_advanced(WiringPi_INCLUDE_DIR WiringPi_LIBRARIES WiringPi_LIBRARY)
