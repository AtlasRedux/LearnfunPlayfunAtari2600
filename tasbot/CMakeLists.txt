cmake_minimum_required(VERSION 3.20)
project(tasbot_atari CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# --- Find packages from vcpkg ---
find_package(Protobuf CONFIG REQUIRED)
find_package(ZLIB REQUIRED)
find_package(lz4 CONFIG REQUIRED)
find_package(PNG)
find_package(SDL2 CONFIG REQUIRED)

# --- Protobuf code generation ---
protobuf_generate_cpp(PROTO_SRCS PROTO_HDRS
    ${CMAKE_CURRENT_SOURCE_DIR}/marionet.proto
)

# --- cc-lib sources ---
set(CCLIB_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../cc-lib")
set(CCLIB_SOURCES
    ${CCLIB_DIR}/util.cc
    ${CCLIB_DIR}/arcfour.cc
    ${CCLIB_DIR}/base/stringprintf.cc
    ${CCLIB_DIR}/city/city.cc
    ${CCLIB_DIR}/textsvg.cc
)
if(EXISTS "${CCLIB_DIR}/stb_image.cc")
    list(APPEND CCLIB_SOURCES ${CCLIB_DIR}/stb_image.cc)
endif()

# --- stella2023 libretro core ---
# Explicit source list from stella-libretro/src/os/libretro/Makefile.common.
# Only the files needed for the libretro core are compiled (no GUI, debugger,
# cheats, sqlite, etc.).
set(STELLA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../stella-libretro/src")
if(EXISTS "${STELLA_DIR}")
    set(STELLA_SOURCES
        # libretro glue
        ${STELLA_DIR}/os/libretro/libretro.cxx
        ${STELLA_DIR}/os/libretro/FSNodeLIBRETRO.cxx
        ${STELLA_DIR}/os/libretro/StellaLIBRETRO.cxx
        # common
        ${STELLA_DIR}/common/AudioQueue.cxx
        ${STELLA_DIR}/common/AudioSettings.cxx
        ${STELLA_DIR}/common/Base.cxx
        ${STELLA_DIR}/common/Bezel.cxx
        ${STELLA_DIR}/common/DevSettingsHandler.cxx
        ${STELLA_DIR}/common/FpsMeter.cxx
        ${STELLA_DIR}/common/FSNodeZIP.cxx
        ${STELLA_DIR}/common/JoyMap.cxx
        ${STELLA_DIR}/common/KeyMap.cxx
        ${STELLA_DIR}/common/Logger.cxx
        ${STELLA_DIR}/common/MouseControl.cxx
        ${STELLA_DIR}/common/PaletteHandler.cxx
        ${STELLA_DIR}/common/PhosphorHandler.cxx
        ${STELLA_DIR}/common/PhysicalJoystick.cxx
        ${STELLA_DIR}/common/PJoystickHandler.cxx
        ${STELLA_DIR}/common/PKeyboardHandler.cxx
        ${STELLA_DIR}/common/RewindManager.cxx
        ${STELLA_DIR}/common/StaggeredLogger.cxx
        ${STELLA_DIR}/common/StateManager.cxx
        ${STELLA_DIR}/common/TimerManager.cxx
        ${STELLA_DIR}/common/VideoModeHandler.cxx
        ${STELLA_DIR}/common/tv_filters/AtariNTSC.cxx
        ${STELLA_DIR}/common/tv_filters/NTSCFilter.cxx
        ${STELLA_DIR}/common/repository/CompositeKeyValueRepository.cxx
        ${STELLA_DIR}/common/repository/CompositeKVRJsonAdapter.cxx
        ${STELLA_DIR}/common/repository/KeyValueRepositoryConfigfile.cxx
        ${STELLA_DIR}/common/repository/KeyValueRepositoryJsonFile.cxx
        ${STELLA_DIR}/common/repository/KeyValueRepositoryPropertyFile.cxx
        # emucore
        ${STELLA_DIR}/emucore/AtariVox.cxx
        ${STELLA_DIR}/emucore/Bankswitch.cxx
        ${STELLA_DIR}/emucore/Booster.cxx
        ${STELLA_DIR}/emucore/Cart.cxx
        ${STELLA_DIR}/emucore/CartCreator.cxx
        ${STELLA_DIR}/emucore/CartDetector.cxx
        ${STELLA_DIR}/emucore/CartEnhanced.cxx
        ${STELLA_DIR}/emucore/Cart03E0.cxx
        ${STELLA_DIR}/emucore/Cart0840.cxx
        ${STELLA_DIR}/emucore/Cart0FA0.cxx
        ${STELLA_DIR}/emucore/Cart2K.cxx
        ${STELLA_DIR}/emucore/Cart3E.cxx
        ${STELLA_DIR}/emucore/Cart3EPlus.cxx
        ${STELLA_DIR}/emucore/Cart3EX.cxx
        ${STELLA_DIR}/emucore/Cart3F.cxx
        ${STELLA_DIR}/emucore/Cart4A50.cxx
        ${STELLA_DIR}/emucore/Cart4K.cxx
        ${STELLA_DIR}/emucore/Cart4KSC.cxx
        ${STELLA_DIR}/emucore/CartAR.cxx
        ${STELLA_DIR}/emucore/CartARM.cxx
        ${STELLA_DIR}/emucore/CartBF.cxx
        ${STELLA_DIR}/emucore/CartBFSC.cxx
        ${STELLA_DIR}/emucore/CartBUS.cxx
        ${STELLA_DIR}/emucore/CartCDF.cxx
        ${STELLA_DIR}/emucore/CartCM.cxx
        ${STELLA_DIR}/emucore/CartCTY.cxx
        ${STELLA_DIR}/emucore/CartCV.cxx
        ${STELLA_DIR}/emucore/CartDF.cxx
        ${STELLA_DIR}/emucore/CartDFSC.cxx
        ${STELLA_DIR}/emucore/CartDPC.cxx
        ${STELLA_DIR}/emucore/CartDPCPlus.cxx
        ${STELLA_DIR}/emucore/CartE0.cxx
        ${STELLA_DIR}/emucore/CartE7.cxx
        ${STELLA_DIR}/emucore/CartEF.cxx
        ${STELLA_DIR}/emucore/CartEFSC.cxx
        ${STELLA_DIR}/emucore/CartF0.cxx
        ${STELLA_DIR}/emucore/CartF4.cxx
        ${STELLA_DIR}/emucore/CartF4SC.cxx
        ${STELLA_DIR}/emucore/CartF6.cxx
        ${STELLA_DIR}/emucore/CartF6SC.cxx
        ${STELLA_DIR}/emucore/CartF8.cxx
        ${STELLA_DIR}/emucore/CartF8SC.cxx
        ${STELLA_DIR}/emucore/CartFA2.cxx
        ${STELLA_DIR}/emucore/CartFA.cxx
        ${STELLA_DIR}/emucore/CartFC.cxx
        ${STELLA_DIR}/emucore/CartFE.cxx
        ${STELLA_DIR}/emucore/CartGL.cxx
        ${STELLA_DIR}/emucore/CartMDM.cxx
        ${STELLA_DIR}/emucore/CartMVC.cxx
        ${STELLA_DIR}/emucore/CartSB.cxx
        ${STELLA_DIR}/emucore/CartTVBoy.cxx
        ${STELLA_DIR}/emucore/CartUA.cxx
        ${STELLA_DIR}/emucore/CartWD.cxx
        ${STELLA_DIR}/emucore/CartX07.cxx
        ${STELLA_DIR}/emucore/CompuMate.cxx
        ${STELLA_DIR}/emucore/Console.cxx
        ${STELLA_DIR}/emucore/Control.cxx
        ${STELLA_DIR}/emucore/ControllerDetector.cxx
        ${STELLA_DIR}/emucore/DispatchResult.cxx
        ${STELLA_DIR}/emucore/Driving.cxx
        ${STELLA_DIR}/emucore/EmulationTiming.cxx
        ${STELLA_DIR}/emucore/EmulationWorker.cxx
        ${STELLA_DIR}/emucore/EventHandler.cxx
        ${STELLA_DIR}/emucore/FBSurface.cxx
        ${STELLA_DIR}/emucore/FrameBuffer.cxx
        ${STELLA_DIR}/emucore/FSNode.cxx
        ${STELLA_DIR}/emucore/Genesis.cxx
        ${STELLA_DIR}/emucore/GlobalKeyHandler.cxx
        ${STELLA_DIR}/emucore/Joy2BPlus.cxx
        ${STELLA_DIR}/emucore/Joystick.cxx
        ${STELLA_DIR}/emucore/Keyboard.cxx
        ${STELLA_DIR}/emucore/KidVid.cxx
        ${STELLA_DIR}/emucore/Lightgun.cxx
        ${STELLA_DIR}/emucore/M6502.cxx
        ${STELLA_DIR}/emucore/M6532.cxx
        ${STELLA_DIR}/emucore/MD5.cxx
        ${STELLA_DIR}/emucore/MindLink.cxx
        ${STELLA_DIR}/emucore/MT24LC256.cxx
        ${STELLA_DIR}/emucore/OSystem.cxx
        ${STELLA_DIR}/emucore/Paddles.cxx
        ${STELLA_DIR}/emucore/PlusROM.cxx
        ${STELLA_DIR}/emucore/PointingDevice.cxx
        ${STELLA_DIR}/emucore/Props.cxx
        ${STELLA_DIR}/emucore/PropsSet.cxx
        ${STELLA_DIR}/emucore/QuadTari.cxx
        ${STELLA_DIR}/emucore/SaveKey.cxx
        ${STELLA_DIR}/emucore/Serializer.cxx
        ${STELLA_DIR}/emucore/Settings.cxx
        ${STELLA_DIR}/emucore/Switches.cxx
        ${STELLA_DIR}/emucore/System.cxx
        ${STELLA_DIR}/emucore/Thumbulator.cxx
        # TIA
        ${STELLA_DIR}/emucore/tia/AudioChannel.cxx
        ${STELLA_DIR}/emucore/tia/Audio.cxx
        ${STELLA_DIR}/emucore/tia/Background.cxx
        ${STELLA_DIR}/emucore/tia/Ball.cxx
        ${STELLA_DIR}/emucore/tia/DrawCounterDecodes.cxx
        ${STELLA_DIR}/emucore/tia/frame-manager/AbstractFrameManager.cxx
        ${STELLA_DIR}/emucore/tia/frame-manager/FrameLayoutDetector.cxx
        ${STELLA_DIR}/emucore/tia/frame-manager/FrameManager.cxx
        ${STELLA_DIR}/emucore/tia/frame-manager/JitterEmulation.cxx
        ${STELLA_DIR}/emucore/tia/LatchedInput.cxx
        ${STELLA_DIR}/emucore/tia/Missile.cxx
        ${STELLA_DIR}/emucore/tia/AnalogReadout.cxx
        ${STELLA_DIR}/emucore/tia/Player.cxx
        ${STELLA_DIR}/emucore/tia/Playfield.cxx
        ${STELLA_DIR}/emucore/TIASurface.cxx
        ${STELLA_DIR}/emucore/tia/TIA.cxx
    )

    add_library(stella_core STATIC ${STELLA_SOURCES})
    target_include_directories(stella_core PUBLIC
        "${STELLA_DIR}"
        "${STELLA_DIR}/os/libretro"
        "${STELLA_DIR}/emucore"
        "${STELLA_DIR}/emucore/tia"
        "${STELLA_DIR}/common"
        "${STELLA_DIR}/common/audio"
        "${STELLA_DIR}/common/tv_filters"
        "${STELLA_DIR}/common/repository/sqlite"
        "${STELLA_DIR}/lib/json"
        "${STELLA_DIR}/lib/httplib"
        "${STELLA_DIR}/lib/sqlite"
    )
    target_compile_definitions(stella_core PUBLIC
        __LIB_RETRO__
        SOUND_SUPPORT
        HAVE_STDINT_H
    )
    if(WIN32)
        target_compile_definitions(stella_core PUBLIC __WIN32__)
    endif()
    # C++17 required by Stella
    target_compile_features(stella_core PUBLIC cxx_std_17)
    if(MSVC)
        target_compile_options(stella_core PRIVATE
            /W1
            /wd4244 /wd4267 /wd4018 /wd4996 /wd4101
            /wd4305 /wd4309 /wd4805 /wd4838 /wd4146
            /wd4715  # not all control paths return a value
            /wd4700  # uninitialized local variable used
            /wd4804  # unsafe use of type bool
        )
    endif()
    set(HAVE_STELLA TRUE)
else()
    message(WARNING
        "stella-libretro source not found at ../stella-libretro/src\n"
        "Please clone: git clone https://github.com/libretro/stella2023.git ../stella-libretro\n"
        "Building without emulator — only library targets will work.")
    set(HAVE_STELLA FALSE)
endif()

# --- TASBot core sources ---
set(TASBOT_CORE_SOURCES
    simpleinput.cc
    emulator.cc
    basis-util.cc
    objective.cc
    weighted-objectives.cc
    motifs.cc
    util.cc
    netutil.cc
    ${PROTO_SRCS}
)

# --- Core static library ---
add_library(tasbot_core STATIC
    ${TASBOT_CORE_SOURCES}
    ${CCLIB_SOURCES}
)

target_include_directories(tasbot_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}/../cc-lib
    ${CMAKE_CURRENT_SOURCE_DIR}/../cc-lib/city
    ${CMAKE_CURRENT_BINARY_DIR}  # for generated .pb.h files
)

target_compile_definitions(tasbot_core PUBLIC
    MARIONET=1
    _CRT_SECURE_NO_WARNINGS
    _WINSOCK_DEPRECATED_NO_WARNINGS
    NOMINMAX
)

# MSVC-specific settings
if(MSVC)
    target_compile_options(tasbot_core PUBLIC
        /W3
        /wd4244  # conversion from larger to smaller type
        /wd4996  # deprecated functions
        /wd4267  # size_t to int conversion
        /wd4018  # signed/unsigned mismatch
        /wd4101  # unreferenced local variable
        /wd4305  # truncation from double to float
    )
endif()

# --- Release optimizations ---
if(MSVC)
    target_compile_options(tasbot_core PUBLIC
        $<$<CONFIG:Release>:/GL>
    )
    target_link_options(tasbot_core PUBLIC
        $<$<CONFIG:Release>:/LTCG>
    )
endif()

target_link_libraries(tasbot_core PUBLIC
    protobuf::libprotobuf
    ZLIB::ZLIB
    lz4::lz4
    ws2_32
)

if(HAVE_STELLA)
    target_link_libraries(tasbot_core PUBLIC stella_core)
endif()

# --- Executables ---

add_executable(learnfun learnfun.cc)
target_link_libraries(learnfun tasbot_core)

add_executable(playfun playfun.cc)
target_link_libraries(playfun tasbot_core)

# Copy the GUI launcher alongside the executables.
add_custom_command(TARGET playfun POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${CMAKE_CURRENT_SOURCE_DIR}/tasbot_gui.pyw"
        "$<TARGET_FILE_DIR:playfun>/tasbot_gui.pyw"
    COMMENT "Copying tasbot_gui.pyw to output directory"
)

add_executable(recordfun recordfun.cc)
target_link_libraries(recordfun tasbot_core SDL2::SDL2 SDL2::SDL2main)

add_executable(replayfun replayfun.cc)
target_link_libraries(replayfun tasbot_core SDL2::SDL2 SDL2::SDL2main)

add_executable(emu_test emu_test.cc)
target_link_libraries(emu_test tasbot_core)

# --- PNG-dependent visualization targets (optional) ---
if(PNG_FOUND)
    add_executable(scopefun scopefun.cc pngsave.cc wave.cc)
    target_link_libraries(scopefun tasbot_core PNG::PNG)

    add_executable(pinviz pinviz.cc pngsave.cc wave.cc)
    target_link_libraries(pinviz tasbot_core PNG::PNG)
endif()
