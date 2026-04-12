cmake_minimum_required(VERSION 3.16)
project(SimulatorUop LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

file(GLOB_RECURSE SIMULATOR_SOURCES CONFIGURE_DEPENDS
	"${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

set(AXI_KIT_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../simulator/axi-interconnect-kit")
set(AXI_KIT_SOURCES
	"${AXI_KIT_ROOT}/axi_interconnect/AXI_LLC.cpp"
	"${AXI_KIT_ROOT}/axi_interconnect/AXI_Interconnect.cpp"
	"${AXI_KIT_ROOT}/axi_interconnect/AXI_Router_AXI4.cpp"
	"${AXI_KIT_ROOT}/sim_ddr/SimDDR.cpp"
	"${AXI_KIT_ROOT}/mmio/MMIO_Bus_AXI4.cpp"
	"${AXI_KIT_ROOT}/mmio/UART16550_Device.cpp")

add_executable(simulator_uop ${SIMULATOR_SOURCES} ${AXI_KIT_SOURCES})

target_include_directories(simulator_uop PRIVATE
	"${CMAKE_CURRENT_SOURCE_DIR}/src"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/front-end/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/back-end/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/back-end/Exu/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/back-end/Lsu/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/diff/include"
	"${CMAKE_CURRENT_SOURCE_DIR}/src/MemSubSystem/include"
	"${AXI_KIT_ROOT}/include"
	"${AXI_KIT_ROOT}/axi_interconnect/include"
	"${AXI_KIT_ROOT}/sim_ddr/include"
	"${AXI_KIT_ROOT}/mmio/include")

target_compile_definitions(simulator_uop PRIVATE
	AXI_KIT_DEBUG=0
	AXI_KIT_MAX_WRITE_TRANSACTION_BYTES=64
	AXI_KIT_SIM_DDR_BEAT_BYTES=4
	AXI_KIT_SIM_DDR_READ_LATENCY=1
	AXI_KIT_SIM_DDR_WRITE_RESP_LATENCY=1)
