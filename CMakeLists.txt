##
## This file is part of the libsigrok project.
##
## Copyright (C) 2020 Uwe Hermann <uwe@hermann-uwe.de>
##
## This program is free software: you can redistribute it and/or modify
## it under the terms of the GNU General Public License as published by
## the Free Software Foundation, either version 2 of the License, or
## (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program.  If not, see <http://www.gnu.org/licenses/>.
##

cmake_minimum_required(VERSION 3.12)

project(sigrok VERSION 6.0.0 LANGUAGES C CXX)

include(GNUInstallDirs)

find_package(Git QUIET)
if(Git_FOUND)
	execute_process(COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} ERROR_QUIET
		OUTPUT_VARIABLE GITHASH OUTPUT_STRIP_TRAILING_WHITESPACE)
	execute_process(COMMAND
		${GIT_EXECUTABLE} name-rev --tags --name-only ${GITHASH}
		WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR} ERROR_QUIET
		OUTPUT_VARIABLE GITTAG OUTPUT_STRIP_TRAILING_WHITESPACE)
	if((DEFINED GITTAG) AND ("${GITTAG}" STREQUAL "undefined"))
		string(APPEND PROJECT_VERSION "-git-${GITHASH}")
	endif()
endif()

option(BUILD_SHARED_LIBS "Shared (non-static) build" TRUE)
option(DISABLE_ALL_DRIVERS "Disable all hardware drivers" FALSE)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
	set_property(CACHE CMAKE_BUILD_TYPE PROPERTY VALUE RelWithDebInfo)
endif()

set_property(GLOBAL PROPERTY USE_FOLDERS TRUE)

find_package(serialport 0.1.2)
message(STATUS "libserialport: ${serialport_VERSION}")

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBGLIB REQUIRED IMPORTED_TARGET "glib-2.0>=2.32.0")
pkg_check_modules(LIBZIP REQUIRED IMPORTED_TARGET "libzip>=0.10")
pkg_check_modules(LIBGIO IMPORTED_TARGET "gio-2.0>=2.24.0")
pkg_check_modules(LIBREVISA IMPORTED_TARGET "librevisa>=0.0.20130412")
pkg_check_modules(LIBGPIB IMPORTED_TARGET "libgpib")
if(WIN32)
	pkg_check_modules(LIBUSB IMPORTED_TARGET "libusb-1.0>=1.0.20")
elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
	pkg_check_modules(LIBUSB IMPORTED_TARGET "libusb-1.0")
else()
	pkg_check_modules(LIBUSB IMPORTED_TARGET "libusb-1.0>=1.0.16")
endif()
pkg_check_modules(LIBFTDI IMPORTED_TARGET "libftdi1>=1.0")
pkg_search_module(LIBHIDAPI IMPORTED_TARGET "hidapi>=0.8.0;hidapi-hidraw>=0.8.0;hidapi-libusb>=0.8.0")
# Ugly workaround, *_MODULE_NAME only exists in cmake >= 3.16.
if(pkgcfg_lib_LIBHIDAPI_hidapi)
	set(LIBHIDAPI_WORKAROUND_MODULE_NAME "hidapi")
elseif(pkgcfg_lib_LIBHIDAPI_hidapi-hidraw)
	set(LIBHIDAPI_WORKAROUND_MODULE_NAME "hidapi-hidraw")
elseif(pkgcfg_lib_LIBHIDAPI_hidapi-libusb)
	set(LIBHIDAPI_WORKAROUND_MODULE_NAME "hidapi-libusb")
endif()
if(LIBHIDAPI_FOUND)
	message(STATUS "  Found ${LIBHIDAPI_WORKAROUND_MODULE_NAME}, version ${LIBHIDAPI_VERSION}")
else()
	message(STATUS "  Package 'hidapi', not found")
endif()
pkg_check_modules(LIBBLUEZ IMPORTED_TARGET "bluez>=4.0")
pkg_check_modules(LIBCHECK IMPORTED_TARGET "check>=0.9.4")

include(TestBigEndian)
test_big_endian(WORDS_BIGENDIAN)

include(CheckIncludeFile)
check_include_file("sys/timerfd.h" SYS_TIMERFD_H_FOUND)
check_include_file("sys/mman.h" SYS_MMAN_H_FOUND)
check_include_file("sys/ioctl.h" SYS_IOCTL_H_FOUND)

include(CheckSymbolExists)
check_symbol_exists("clnt_create" "rpc/rpc.h" HAVE_RPC)
check_symbol_exists("bt_put_le16" "bluetooth/bluetooth.h" HAVE_BT_PUT_LE16)
set(CMAKE_REQUIRED_LIBRARIES "zip")
check_symbol_exists("zip_discard" "zip.h" HAVE_ZIP_DISCARD)
unset(CMAKE_REQUIRED_LIBRARIES)

set(CMAKE_CXX_STANDARD 11)
include(CheckCXXSourceCompiles)
check_cxx_source_compiles("#include <string>
int main() { return std::stoi(\"1\") + std::stod(\"1.0\"); }" HAVE_STOI_STOD)
unset(CMAKE_CXX_STANDARD)

include(CheckTypeSize)
check_type_size("__int128_t" HAVE___INT128_T)
check_type_size("__uint128_t" HAVE___UINT128_T)

include(CheckLibraryExists)
check_library_exists("ieee1284" "ieee1284_open" "" LIBIEEE1284_FOUND) # TODO

if(serialport_FOUND OR LIBHIDAPI_FOUND OR LIBBLUEZ_FOUND)
	set(HAVE_SERIAL_COMM 1)
	set(SERIAL_COMM_FOUND 1)
endif()
if(LIBBLUEZ_FOUND)
	set(HAVE_BLUETOOTH 1)
endif()

# TODO: Eliminate later.
set(HAVE_LIBBLUEZ ${LIBBLUEZ_FOUND})
set(HAVE_LIBFTDI ${LIBFTDI_FOUND})
set(HAVE_LIBGIO ${LIBGIO_FOUND})
set(HAVE_LIBGPIB ${LIBGPIB_FOUND})
set(HAVE_LIBHIDAPI ${LIBHIDAPI_FOUND})
set(HAVE_LIBREVISA ${LIBREVISA_FOUND})
set(HAVE_LIBREVISA ${LIBREVISA_FOUND})
set(HAVE_LIBSERIALPORT ${serialport_FOUND})
set(HAVE_LIBUSB_1_0 ${LIBUSB_FOUND})

add_library(${PROJECT_NAME} "")
add_library(sigrok::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

function(sr_files cond subdir files)
	if(${cond})
		# set(L "")
		foreach(f ${files})
			string(APPEND L "${subdir}/${f};")
		endforeach()
		target_sources(${PROJECT_NAME} PRIVATE ${L})
		source_group(TREE ${CMAKE_CURRENT_SOURCE_DIR} FILES ${L})
	endif()
endfunction()

sr_files(TRUE "src" "backend.c;conversion.c;device.c;session.c;session_file.c;session_driver.c;hwdriver.c;trigger.c;soft-trigger.c;analog.c;fallback.c;resource.c;strutil.c;log.c;version.c;error.c;std.c;sw_limits.c;scpi.h;scpi/scpi.c;scpi/scpi_tcp.c;driver_list_start.c;driver_list_stop.c;drivers.c;modbus/modbus.c;scale/kern.c")
sr_files(TRUE "src/input" "input.c;binary.c;chronovu_la8.c;csv.c;logicport.c;raw_analog.c;trace32_ad.c;vcd.c;wav.c;null.c")
sr_files(TRUE "src/output" "output.c;analog.c;ascii.c;bits.c;binary.c;csv.c;chronovu_la8.c;wav.c;hex.c;ols.c;srzip.c;vcd.c;wavedrom.c;null.c")
sr_files(TRUE "src/transform" "transform.c;nop.c;scale.c;invert.c")
sr_files(TRUE "src/dmm" "asycii.c;bm25x.c;bm86x.c;dtm0660.c;eev121gw.c;es519xx.c;fs9721.c;fs9922.c;m2110.c;metex14.c;ms2115b.c;ms8250d.c;rs9lcd.c;ut372.c;ut71x.c;vc870.c;vc96.c")
sr_files(HAVE_RPC "src/scpi" "scpi_vxi.c;vxi_clnt.c;vxi_xdr.c;vxi.h")
sr_files(SERIAL_COMM_FOUND "src" "serial.c;serial_bt.c;serial_hid.c;serial_hid.h;serial_hid_bu86x.c;serial_hid_ch9325.c;serial_hid_cp2110.c;serial_hid_victor.c;serial_libsp.c;scpi/scpi_serial.c;modbus/modbus_serial_rtu.c;lcr/es51919.c;lcr/vc4080.c")
sr_files(LIBBLUEZ_FOUND "src/bt" "bt_bluez.c")
sr_files(LIBUSB_FOUND "src" "ezusb.c;usb.c;scpi/scpi_usbtmc_libusb.c")
sr_files(LIBREVISA_FOUND "src/scpi" "scpi_visa.c")
sr_files(LIBGPIB_FOUND "src/scpi" "scpi_libgpib.c")

set(DRIVERDIRS "agilent-dmm;appa-55ii;arachnid-labs-re-load-pro;asix-sigma;atten-pps3xxx;baylibre-acme;beaglelogic;brymen-dmm;cem-dt-885x;center-3xx;chronovu-la;colead-slm;conrad-digi-35-cpu;demo;dreamsourcelab-dslogic;fluke-45;fluke-dmm;ftdi-la;fx2lafw;gmc-mh-1x-2x;gwinstek-gds-800;gwinstek-gpd;hameg-hmo;hantek-4032l;hantek-6xxx;hantek-dso;hp-3457a;hp-3478a;hung-chang-dso-2100;ikalogic-scanalogic2;ikalogic-scanaplus;ipdbg-la;kecheng-kc-330b;kern-scale;korad-kaxxxxp;lascar-el-usb;lecroy-logicstudio;lecroy-xstream;manson-hcs-3xxx;mastech-ms6514;maynuo-m97;mic-985xx;microchip-pickit2;mooshimeter-dmm;motech-lps-30x;norma-dmm;openbench-logic-sniffer;pce-322a;pipistrello-ols;rdtech-dps;rigol-ds;rohde-schwarz-sme-0x;saleae-logic16;saleae-logic-pro;scpi-dmm;scpi-pps;serial-dmm;serial-lcr;siglent-sds;sysclk-lwla;sysclk-sla5032;teleinfo;testo;tondaj-sl-814;uni-t-dmm;uni-t-ut32x;yokogawa-dlm;zeroplus-logic-cube;zketech-ebd-usb")

set(SERIAL_COMM_DRIVERS "agilent-dmm;appa-55ii;arachnid-labs-re-load-pro;atten-pps3xxx;brymen-dmm;cem-dt-885x;center-3xx;colead-slm;conrad-digi-35-cpu;fluke-dmm;gmc-mh-1x-2x;gwinstek-gds-800;gwinstek-gpd;hameg-hmo;kern-scale;korad-kaxxxxp;manson-hcs-3xxx;mastech-ms6514;mic-985xx;motech-lps-30x;norma-dmm;openbench-logic-sniffer;pce-322a;rdtech-dps;rohde-schwarz-sme-0x;serial-dmm;serial-lcr;teleinfo;tondaj-sl-814;uni-t-ut32x;zketech-ebd-usb")
set(LIBUSB_DRIVERS "chronovu-la;dreamsourcelab-dslogic;ftdi-la;fx2lafw;hantek-4032l;hantek-6xxx;hantek-dso;ikalogic-scanalogic2;ikalogic-scanaplus;kecheng-kc-330b;lascar-el-usb;lecroy-logicstudio;microchip-pickit2;saleae-logic16;saleae-logic-pro;sysclk-lwla;sysclk-sla5032;testo;uni-t-dmm;zeroplus-logic-cube")
set(LIBFTDI_DRIVERS "asix-sigma;ftdi-la;pipistrello-ols")
set(LIBGPIB_DRIVERS "hp-3478a")
set(LIBIEEE1284_DRIVERS "hung-chang-dso-2100")
set(LIBBLUEZ_DRIVERS "mooshimeter-dmm")
set(LIBGIO_DRIVERS "mooshimeter-dmm")
set(SYS_TIMERFD_H_DRIVERS "baylibre-acme")
set(SYS_MMAN_H_DRIVERS "beaglelogic")
set(SYS_IOCTL_H_DRIVERS "beaglelogic")

set(BAYLIBRE_ACME_FILES "gpio.c;gpio.h")
set(BEAGLELOGIC_FILES "beaglelogic.h;beaglelogic_native.c;beaglelogic_tcp.c")
set(BRYMEN_DMM_FILES "parser.c")
set(SCPI_PPS_FILES "profiles.c")
set(SYSCLK_LWLA_FILES "lwla.c;lwla.h;lwla1016.c;lwla1034.c")
set(YOKOGAWA_DLM_FILES "protocol_wrappers.c;protocol_wrappers.h")
set(ZEROPLUS_LOGIC_CUBE_FILES "analyzer.c;analyzer.h;gl_usb.c;gl_usb.h")

macro(sr_disable_driver drivers DRIVER d found)
	list(FIND ${drivers} ${d} result)
	if(((NOT ${result} EQUAL -1) AND NOT ${found}) OR DISABLE_ALL_DRIVERS)
		set(ENABLE_${${DRIVER}} OFF)
		string(APPEND disabled_drivers "\n     ${d} (!${found})")
	endif()
endmacro()

foreach(d ${DRIVERDIRS})
	string(REPLACE "-" "_" driver_underscore ${d})
	string(TOUPPER ${driver_underscore} DRIVER)
	option(ENABLE_${DRIVER} "Build this hardware driver" ON)
	foreach(p SERIAL_COMM;LIBUSB;LIBFTDI;LIBGPIB;LIBIEEE1284;LIBBLUEZ;LIBGIO;SYS_TIMERFD_H;SYS_MMAN_H;SYS_IOCTL_H)
		sr_disable_driver(${p}_DRIVERS DRIVER ${d} ${p}_FOUND)
	endforeach()
	if(ENABLE_${DRIVER})
		sr_files(TRUE "src/hardware/${d}"
			"api.c;protocol.c;protocol.h;${${DRIVER}_FILES}")
	endif()
endforeach()

if(NOT DISABLE_ALL_DRIVERS)
	set(HAVE_DRIVERS 1)
else()
	set(disabled_drivers "ALL")
endif()

set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER "\
${CMAKE_CURRENT_SOURCE_DIR}/include/lib${PROJECT_NAME}/lib${PROJECT_NAME}.h;\
${CMAKE_CURRENT_BINARY_DIR}/include/lib${PROJECT_NAME}/lib${PROJECT_NAME}_version.h;\
${CMAKE_CURRENT_BINARY_DIR}/include/lib${PROJECT_NAME}/lib${PROJECT_NAME}_export.h")

set_target_properties(${PROJECT_NAME} PROPERTIES C_STANDARD 99
	VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

target_compile_options(${PROJECT_NAME} PRIVATE
	$<IF:$<C_COMPILER_ID:MSVC>,/W4,-Wall -Wextra>)

target_compile_definitions(${PROJECT_NAME} PRIVATE
	$<IF:$<BOOL:${MINGW}>,__STDC_FORMAT_MACROS -D__STDC_LIMIT_MACROS -D_POSIX -D__printf__=__gnu_printf__,>)

function(sr_pkgconfig_lib p)
	if(${p}_FOUND AND BUILD_SHARED_LIBS)
		target_link_libraries(${PROJECT_NAME} PUBLIC PkgConfig::${p})
	elseif(${p}_FOUND)
		target_include_directories(${PROJECT_NAME} PUBLIC ${${p}_STATIC_INCLUDE_DIRS})
		target_link_libraries(${PROJECT_NAME} PUBLIC ${${p}_STATIC_LDFLAGS})
	endif()
endfunction()

# Ugly workaround, *_MODULE_NAME only exists in cmake >= 3.16.
set(LIBGLIB_WORKAROUND_MODULE_NAME "glib-2.0")
set(LIBZIP_WORKAROUND_MODULE_NAME "libzip")
set(LIBGIO_WORKAROUND_MODULE_NAME "gio-2.0")
set(LIBUSB_WORKAROUND_MODULE_NAME "libusb-1.0")
set(LIBFTDI_WORKAROUND_MODULE_NAME "libftdi1")
set(LIBREVISA_WORKAROUND_MODULE_NAME "librevisa")
set(LIBGPIB_WORKAROUND_MODULE_NAME "libgpib")
set(LIBBLUEZ_WORKAROUND_MODULE_NAME "bluez")
foreach(p GLIB;ZIP;GIO;USB;FTDI;HIDAPI;REVISA;GPIB;BLUEZ)
	sr_pkgconfig_lib(LIB${p})
	if(LIB${p}_FOUND)
		string(APPEND SR_PKGLIBS "${LIB${p}_WORKAROUND_MODULE_NAME} ")
	endif()
endforeach()

target_link_libraries(${PROJECT_NAME} PUBLIC m)

if(serialport_FOUND)
	target_link_libraries(${PROJECT_NAME} PUBLIC serialport::serialport)
	string(APPEND SR_PKGLIBS "libserialport")
endif()
if(LIBIEEE1284_FOUND)
	target_link_libraries(${PROJECT_NAME} PUBLIC ieee1284)
	string(APPEND SR_EXTRA_LIBS "-lieee1284 ")
endif()
if(WIN32)
	target_link_libraries(${PROJECT_NAME} PUBLIC ws2_32)
	string(APPEND SR_EXTRA_LIBS "-lws2_32 ")
endif()

target_include_directories(${PROJECT_NAME} PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/include>
	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
	PRIVATE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)

set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(VISIBILITY_INLINES_HIDDEN TRUE)
include(GenerateExportHeader)
generate_export_header(${PROJECT_NAME} BASE_NAME lib${PROJECT_NAME}
	PREFIX_NAME "SR_" EXPORT_MACRO_NAME "API" NO_EXPORT_MACRO_NAME "PRIV"
	DEPRECATED_MACRO_NAME "DEPR" NO_DEPRECATED_MACRO_NAME "NO_DEPR"
	EXPORT_FILE_NAME ${CMAKE_CURRENT_BINARY_DIR}/include/lib${PROJECT_NAME}/lib${PROJECT_NAME}_export.h)

include(CMakePackageConfigHelpers)
set(INST_INC_DIR ${CMAKE_INSTALL_INCLUDEDIR})
set(INST_LIB_DIR ${CMAKE_INSTALL_LIBDIR})
configure_package_config_file(${PROJECT_NAME}Config.cmake.in
	${PROJECT_NAME}Config.cmake PATH_VARS INST_INC_DIR INST_LIB_DIR
	INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
write_basic_package_version_file(
	${PROJECT_NAME}ConfigVersion.cmake COMPATIBILITY SameMajorVersion)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Targets
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lib${PROJECT_NAME})

install(EXPORT ${PROJECT_NAME}Targets
	FILE ${PROJECT_NAME}Targets.cmake NAMESPACE sigrok::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
	${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/lib${PROJECT_NAME}.pc
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

install(FILES contrib/vnd.${PROJECT_NAME}.session.xml DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/mime/packages)
install(FILES contrib/lib${PROJECT_NAME}.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/48x48/mimetypes)
install(FILES contrib/lib${PROJECT_NAME}.svg DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/mimetypes)

message(STATUS "CMake: ${CMAKE_COMMAND} ${CMAKE_VERSION}")
message(STATUS "Building lib${PROJECT_NAME} ${PROJECT_VERSION} for ${CMAKE_SYSTEM} ${CMAKE_SYSTEM_PROCESSOR}")

include(CTest)
if(LIBCHECK_FOUND AND BUILD_TESTING)
	enable_testing()
	add_executable(unittests tests/analog.c tests/conv.c tests/core.c
		tests/device.c tests/driver_all.c tests/input_all.c
		tests/input_binary.c tests/lib.c tests/lib.h tests/main.c
		tests/output_all.c tests/session.c tests/strutil.c
		tests/transform_all.c tests/trigger.c tests/version.c)
	set_target_properties(unittests PROPERTIES C_STANDARD 99)
	target_include_directories(unittests PRIVATE
		$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>)
	target_compile_options(unittests PRIVATE
		$<IF:$<C_COMPILER_ID:MSVC>,/W4,-Wall -Wextra>)
	target_link_libraries(unittests PRIVATE ${PROJECT_NAME} PkgConfig::LIBCHECK)
	add_test(NAME unittests COMMAND unittests)
endif()

set(CPACK_SOURCE_PACKAGE_FILE_NAME "lib${PROJECT_NAME}-${PROJECT_VERSION}")
set(CPACK_SOURCE_IGNORE_FILES ${CMAKE_CURRENT_BINARY_DIR} ".git*")
set(CPACK_SOURCE_GENERATOR "TGZ")
include(CPack)

configure_file(config.h.in config.h @ONLY)
configure_file(include/lib${PROJECT_NAME}/lib${PROJECT_NAME}_version.h.in
	include/lib${PROJECT_NAME}/lib${PROJECT_NAME}_version.h @ONLY)
configure_file(lib${PROJECT_NAME}.pc.in lib${PROJECT_NAME}.pc @ONLY)

message(STATUS "Disabled hardware drivers: ${disabled_drivers}")

#-----------------------------------------------------------------------------

project(sigrokcxx VERSION 6.0.0 LANGUAGES CXX)

if(Git_FOUND)
	if((DEFINED GITTAG) AND ("${GITTAG}" STREQUAL "undefined"))
		string(APPEND PROJECT_VERSION "-git-${GITHASH}")
	endif()
endif()

pkg_check_modules(LIBGLIBMM IMPORTED_TARGET "glibmm-2.4>=2.32.0")

find_package(Doxygen COMPONENTS doxygen dot)
find_package(Python3 COMPONENTS Interpreter)

if(NOT (LIBGLIBMM_FOUND AND DOXYGEN_FOUND AND Python3_Interpreter_FOUND))
	message(FATAL_ERROR "libsigrokcxx requirements not met, aborting.")
endif()

add_custom_command(OUTPUT doxy/xml/index.xml
	DEPENDS include/libsigrok/libsigrok.h
	COMMAND SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/
		BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/
		# Doxygen::doxygen Doxyfile
		${DOXYGEN_EXECUTABLE} Doxyfile 2>/dev/null
	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

# add_custom_command(OUTPUT bindings/cxx/doxy/xml/index.xml
# 	DEPENDS bindings/cxx/include/libsigrokcxx/libsigrokcxx.hpp
# 		${CMAKE_CURRENT_BINARY_DIR}/bindings/cxx/include/libsigrokcxx/enums.hpp
# 	COMMAND SRCDIR=${CMAKE_CURRENT_SOURCE_DIR}/bindings/cxx/
# 		BUILDDIR=${CMAKE_CURRENT_BINARY_DIR}/bindings/cxx/
# 		# Doxygen::doxygen Doxyfile 2>/dev/null
# 		${DOXYGEN_EXECUTABLE} Doxyfile 2>/dev/null
# 	WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/bindings/cxx)

add_custom_command(OUTPUT bindings/cxx/include/libsigrokcxx/enums.hpp
	DEPENDS bindings/cxx/enums.py
		${CMAKE_CURRENT_BINARY_DIR}/doxy/xml/index.xml
		bindings/cxx/ConfigKey_methods.cpp
		bindings/cxx/QuantityFlag_methods.cpp
	COMMAND Python3::Interpreter
		${CMAKE_CURRENT_SOURCE_DIR}/bindings/cxx/enums.py
		${CMAKE_CURRENT_BINARY_DIR}/doxy/xml/index.xml)

add_library(${PROJECT_NAME} "")
add_library(sigrok::${PROJECT_NAME} ALIAS ${PROJECT_NAME})

target_sources(${PROJECT_NAME} PRIVATE bindings/cxx/classes.cpp
	${CMAKE_CURRENT_BINARY_DIR}/bindings/cxx/include/lib${PROJECT_NAME}/enums.hpp)

set_target_properties(${PROJECT_NAME} PROPERTIES PUBLIC_HEADER
"bindings/cxx/include/lib${PROJECT_NAME}/lib${PROJECT_NAME}.hpp;\
${CMAKE_CURRENT_BINARY_DIR}/bindings/cxx/include/lib${PROJECT_NAME}/enums.hpp")

set_target_properties(${PROJECT_NAME} PROPERTIES
	VERSION ${PROJECT_VERSION} SOVERSION ${PROJECT_VERSION_MAJOR})

target_compile_features(${PROJECT_NAME} PUBLIC cxx_std_11)

target_include_directories(${PROJECT_NAME} PUBLIC
	$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/bindings/cxx>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/bindings/cxx/include>
	$<BUILD_INTERFACE:${CMAKE_CURRENT_BINARY_DIR}/bindings/cxx/include>
 	$<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)

target_link_libraries(${PROJECT_NAME} PUBLIC sigrok::sigrok PkgConfig::LIBGLIBMM)

include(CMakePackageConfigHelpers)
set(INST_INC_DIR ${CMAKE_INSTALL_INCLUDEDIR})
set(INST_LIB_DIR ${CMAKE_INSTALL_LIBDIR})
configure_package_config_file(${PROJECT_NAME}Config.cmake.in
	${PROJECT_NAME}Config.cmake PATH_VARS INST_INC_DIR INST_LIB_DIR
	INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})
write_basic_package_version_file(
	${PROJECT_NAME}ConfigVersion.cmake COMPATIBILITY SameMajorVersion)

install(TARGETS ${PROJECT_NAME} EXPORT ${PROJECT_NAME}Targets
	RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
	LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
	ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
	PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/lib${PROJECT_NAME})

install(EXPORT ${PROJECT_NAME}Targets
	FILE ${PROJECT_NAME}Targets.cmake NAMESPACE sigrok::
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}Config.cmake
	${CMAKE_CURRENT_BINARY_DIR}/${PROJECT_NAME}ConfigVersion.cmake
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/${PROJECT_NAME})

install(FILES ${CMAKE_CURRENT_BINARY_DIR}/bindings/cxx/lib${PROJECT_NAME}.pc
	DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig)

message(STATUS "Building lib${PROJECT_NAME} ${PROJECT_VERSION} for ${CMAKE_SYSTEM} ${CMAKE_SYSTEM_PROCESSOR}")

configure_file(bindings/cxx/lib${PROJECT_NAME}.pc.in bindings/cxx/lib${PROJECT_NAME}.pc @ONLY)
