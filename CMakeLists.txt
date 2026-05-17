cmake_minimum_required(VERSION 3.20.0)

# Use J-Link for flashing by default to avoid nrfutil-device runtime crash
# seen on some Windows setups with the nRF54L15 DK.
set(BOARD_FLASH_RUNNER jlink)

# Register project-local board root so west finds boards/.
list(APPEND BOARD_ROOT ${CMAKE_CURRENT_SOURCE_DIR})

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})

project(BLE-NFC-EMULATOR LANGUAGES C CXX)

# Enforce C++11 for all C++ sources.
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- Sources -----------------------------------------------------------------
target_sources(app PRIVATE
  src/main.cpp
  src/zbus_channels.c
)

# Application + middleware pipeline (tag manager, BLE peripheral).
if(CONFIG_INCLUDE_APP_PIPELINE)
  target_sources(app PRIVATE
    src/modules/app/tag_manager.cpp
  )

  if(CONFIG_BT)
    target_sources(app PRIVATE
      src/modules/middleware/ble_peripheral.cpp
    )
  endif()
endif()

# PN532 NFC reader driver.
if(CONFIG_NFC_PN532)
  target_sources(app PRIVATE src/modules/drivers/pn532/nfc_reader_pn532.cpp)
endif()

# Optional bring-up helpers.
if(CONFIG_NFC_TEST_OBSERVER)
  target_sources(app PRIVATE src/modules/app/nfc_event_probe.cpp)
endif()

if(CONFIG_I2C_SCANNER_ON_BOOT)
  target_sources(app PRIVATE src/modules/app/i2c_scanner.cpp)
endif()

# Allow #include paths relative to src/.
target_include_directories(app PRIVATE src)
