# ===============================
# Project
# ===============================
TARGET := infusion-pump-app
CORE_LIB := libinfusion_core.a

BUILD_DIR := build
OBJ_DIR := $(BUILD_DIR)/obj

# ===============================
# Toolchain
# ===============================
CXX ?= g++
CC  ?= gcc
AR  ?= ar

# ===============================
# INCLUDES
# ===============================
INC_FLAGS := \
	-Ihal/gpio \
	-Ihal/spi \
	-Ihal/i2c \
	-Idrivers \
	-Iutl \
	-Iservices \
	-Iserver_layer \
	-Isystem \
	-Iuser_application

# Se STRIP_OPT não for definido externamente, usa -s (comportamento local)
STRIP_OPT ?= -s

CXXFLAGS += -Wall -O2 -std=c++17 $(STRIP_OPT) $(INC_FLAGS)
CFLAGS   += -Wall -O2 -std=c99 $(STRIP_OPT) $(INC_FLAGS)

# ===============================
# MAIN DAEMON 
# ===============================
# 1. HAL
HAL_SRCS := \
	hal/gpio/hal_gpio.cpp \
	hal/spi/hal_spi.cpp \
	hal/i2c/hal_i2c.cpp

# 2. Drivers
DRIVER_CPP_SRCS := drivers/stm32_bridge.cpp
DRIVER_C_SRCS := drivers/cmd.c 

UTL_C_SRCS := \
	utl/utl_io.c \
	utl/utl_crc16.c

# 3. Services
SERVICE_SRCS := services/infusion_manager.cpp

# 4. App Main
APP_SRCS := system/main.cpp

# Agrupamento Core
CORE_CPP_SRCS := $(HAL_SRCS) $(DRIVER_CPP_SRCS) $(SERVICE_SRCS)
CORE_C_SRCS   := $(DRIVER_C_SRCS) $(UTL_C_SRCS)

# Objetos Core
CORE_OBJS := $(CORE_CPP_SRCS:%.cpp=$(OBJ_DIR)/%.o) $(CORE_C_SRCS:%.c=$(OBJ_DIR)/%.o)
APP_OBJS  := $(APP_SRCS:%.cpp=$(OBJ_DIR)/%.o)

# Libs Core
LIBS := -lgpiod -lsystemd -lboost_system -lboost_thread -lboost_json \
    -lpthread -latomic -li2c


# ===============================
# FIRMWARE UPDATER (CLI)
# ===============================
UPDATER_TARGET := stm32-updater

UPDATER_CPP_SRCS := update_dir/ota_handler.cpp \
                    hal/gpio/hal_gpio.cpp

UPDATER_C_SRCS   := utl/utl_crc16.c

UPDATER_OBJS := $(UPDATER_CPP_SRCS:%.cpp=$(OBJ_DIR)/%.o) \
                $(UPDATER_C_SRCS:%.c=$(OBJ_DIR)/%.o)

UPDATER_LIBS := -lgpiod -lstdc++


# ===============================
# BENCH (harness de medicao)
# ===============================
# Linka so o que precisa: HAL + bridge + protocolo. Sem boost, sem
# systemd -- quanto menos no processo, menos ruido na medida.
BENCH_TARGET := argus-bench

BENCH_CPP_SRCS := tools/argus_bench.cpp \
                  drivers/stm32_bridge.cpp \
                  hal/spi/hal_spi.cpp \
                  hal/gpio/hal_gpio.cpp

BENCH_C_SRCS   := drivers/cmd.c \
                  utl/utl_io.c \
                  utl/utl_crc16.c

BENCH_OBJS := $(BENCH_CPP_SRCS:%.cpp=$(OBJ_DIR)/bench/%.o) \
              $(BENCH_C_SRCS:%.c=$(OBJ_DIR)/bench/%.o)

BENCH_LIBS := -lgpiod -lstdc++


# ===============================
# ATTACK (cenarios de ataque)
# ===============================
ATTACK_TARGET := argus-attack

ATTACK_CPP_SRCS := tools/argus_attack.cpp \
                   drivers/stm32_bridge.cpp \
                   hal/spi/hal_spi.cpp \
                   hal/gpio/hal_gpio.cpp

ATTACK_C_SRCS   := drivers/cmd.c \
                   utl/utl_io.c \
                   utl/utl_crc16.c

ATTACK_OBJS := $(ATTACK_CPP_SRCS:%.cpp=$(OBJ_DIR)/attack/%.o) \
               $(ATTACK_C_SRCS:%.c=$(OBJ_DIR)/attack/%.o)

ATTACK_LIBS := -lgpiod -lstdc++


# ===============================
# Rules
# ===============================

all: $(TARGET) $(UPDATER_TARGET) $(BENCH_TARGET) $(ATTACK_TARGET)

# Create Static Lib (Core)
$(CORE_LIB): $(CORE_OBJS)
	@echo "Archiving $@"
	$(AR) rcs $@ $^

# Link the Main App
$(TARGET): $(CORE_LIB) $(APP_OBJS)
	@echo "Linking $@"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(APP_OBJS) $(CORE_LIB) -o $@ $(LIBS)

# Link the Updater
$(UPDATER_TARGET): $(UPDATER_OBJS)
	@echo "Linking $@"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(UPDATER_OBJS) -o $@ $(UPDATER_LIBS)

# Link do attack
$(ATTACK_TARGET): $(ATTACK_OBJS)
	@echo "Linking $@"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(ATTACK_OBJS) -o $@ $(ATTACK_LIBS)

$(OBJ_DIR)/attack/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling C++ (attack): $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/attack/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "Compiling C   (attack): $<"
	$(CC) $(CFLAGS) -c $< -o $@

# Link do bench
$(BENCH_TARGET): $(BENCH_OBJS)
	@echo "Linking $@"
	$(CXX) $(CXXFLAGS) $(LDFLAGS) $(BENCH_OBJS) -o $@ $(BENCH_LIBS)

$(OBJ_DIR)/bench/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling C++ (bench): $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/bench/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "Compiling C   (bench): $<"
	$(CC) $(CFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo "Compiling C++: $<"
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	@echo "Compiling C:   $<"
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -rf $(BUILD_DIR) $(CORE_LIB) $(TARGET) $(UPDATER_TARGET) $(BENCH_TARGET) $(ATTACK_TARGET)

.PHONY: all clean
