cmake_minimum_required(VERSION 3.24)
if(APPLE)
  set(CMAKE_OSX_DEPLOYMENT_TARGET "11.0" CACHE STRING "Minimum macOS")
endif()
project(ToneDock VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
include(FetchContent)
include(CTest)
FetchContent_Declare(JUCE GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
  GIT_TAG 29396c22c93392d6738e021b83196283d6e4d850)
FetchContent_MakeAvailable(JUCE)
FetchContent_Declare(nam GIT_REPOSITORY https://github.com/sdatkinson/NeuralAmpModelerCore.git
  GIT_TAG 0b3d3c97b0859a3a8c92a8628c4dd89a25eb5842
  GIT_SUBMODULES Dependencies/eigen Dependencies/AudioDSPTools
  SOURCE_SUBDIR tonedock-no-upstream-tools)
# Download the core without adding its unrelated benchmark executables.
FetchContent_MakeAvailable(nam)
file(GLOB NAM_SOURCES CONFIGURE_DEPENDS "${nam_SOURCE_DIR}/NAM/*.cpp" "${nam_SOURCE_DIR}/NAM/*/*.cpp")
add_library(tonedock_nam STATIC ${NAM_SOURCES})
add_library(tonedock_nam_headers INTERFACE)
target_include_directories(tonedock_nam_headers SYSTEM INTERFACE "${nam_SOURCE_DIR}" "${nam_SOURCE_DIR}/Dependencies/eigen"
  "${nam_SOURCE_DIR}/Dependencies/nlohmann" "${nam_SOURCE_DIR}/Dependencies/AudioDSPTools/dsp")
target_compile_definitions(tonedock_nam_headers INTERFACE NAM_SAMPLE_FLOAT NAM_ENABLE_A2_FAST)
target_link_libraries(tonedock_nam PUBLIC tonedock_nam_headers)
if(MSVC)
  target_compile_options(tonedock_nam PRIVATE /bigobj /O2)
  target_compile_definitions(tonedock_nam PUBLIC NOMINMAX WIN32_LEAN_AND_MEAN _USE_MATH_DEFINES)
else()
  target_compile_options(tonedock_nam PRIVATE -O3)
endif()
set(TONEDOCK_FORMATS VST3)
if(APPLE)
  list(APPEND TONEDOCK_FORMATS AU)
endif()
juce_add_plugin(ToneDock COMPANY_NAME "ToneDock" BUNDLE_ID "org.tonedock.effect"
  PLUGIN_MANUFACTURER_CODE Tndk PLUGIN_CODE Tnd1
  FORMATS ${TONEDOCK_FORMATS} PRODUCT_NAME "ToneDock"
  IS_SYNTH FALSE NEEDS_MIDI_INPUT FALSE NEEDS_MIDI_OUTPUT FALSE IS_MIDI_EFFECT FALSE
  VST3_CATEGORIES Fx Distortion COPY_PLUGIN_AFTER_BUILD FALSE)
juce_generate_juce_header(ToneDock)
file(GLOB T3K_SOURCES CONFIGURE_DEPENDS third_party/t3k/*.cpp)
target_sources(ToneDock PRIVATE src/Library.cpp src/Engine.cpp src/Processor.cpp src/Editor.cpp ${T3K_SOURCES})
target_include_directories(ToneDock PUBLIC src third_party/t3k "${CMAKE_CURRENT_BINARY_DIR}/ToneDock_artefacts/JuceLibraryCode")
set(TONEDOCK_DEFINITIONS JUCE_WEB_BROWSER=0 JUCE_USE_CURL=$<BOOL:$<PLATFORM_ID:Linux>>
  JUCE_ASIO=$<BOOL:$<PLATFORM_ID:Windows>>
  DONT_SET_USING_JUCE_NAMESPACE=1
  JUCE_VST3_CAN_REPLACE_VST2=0 JUCE_MODAL_LOOPS_PERMITTED=0 T3K_EMBEDDED_WEBVIEW=0
  JUCE_APPLICATION_NAME_STRING="ToneDock" JUCE_APPLICATION_VERSION_STRING="${PROJECT_VERSION}")
target_compile_definitions(ToneDock PUBLIC ${TONEDOCK_DEFINITIONS})
target_link_libraries(ToneDock PUBLIC "$<LINK_LIBRARY:WHOLE_ARCHIVE,tonedock_nam>" juce::juce_audio_utils juce::juce_dsp juce::juce_cryptography
  juce::juce_recommended_config_flags)
if(APPLE)
  target_link_libraries(ToneDock PUBLIC "-framework Security")
elseif(WIN32)
  target_link_libraries(ToneDock PUBLIC crypt32 bcrypt)
elseif(UNIX)
  find_package(CURL REQUIRED)
  target_link_libraries(ToneDock PUBLIC CURL::libcurl)
endif()
juce_add_gui_app(ToneDockApp PRODUCT_NAME "ToneDock" COMPANY_NAME "ToneDock" BUNDLE_ID "org.tonedock.app"
  MICROPHONE_PERMISSION_ENABLED TRUE MICROPHONE_PERMISSION_TEXT "ToneDock processes audio from your guitar interface."
  PLIST_TO_MERGE "${CMAKE_CURRENT_SOURCE_DIR}/packaging/Info.plist")
target_sources(ToneDockApp PRIVATE src/Main.cpp)
add_library(tonedock_link_only INTERFACE)
target_link_libraries(tonedock_link_only INTERFACE "$<LINK_ONLY:ToneDock>")
function(tonedock_link_application target)
  # Share processor code, without inheriting plugin-wrapper application macros.
  target_link_libraries(${target} PRIVATE tonedock_link_only juce::juce_audio_utils juce::juce_dsp
    juce::juce_cryptography juce::juce_recommended_config_flags)
  target_include_directories(${target} PRIVATE src third_party/t3k "${CMAKE_CURRENT_BINARY_DIR}/ToneDock_artefacts/JuceLibraryCode")
  target_compile_definitions(${target} PRIVATE ${TONEDOCK_DEFINITIONS})
endfunction()
tonedock_link_application(ToneDockApp)
if(BUILD_TESTING)
  juce_add_console_app(ToneDockTests PRODUCT_NAME "ToneDock tests")
  target_sources(ToneDockTests PRIVATE tests/CoreTests.cpp)
  tonedock_link_application(ToneDockTests)
  target_link_libraries(ToneDockTests PRIVATE tonedock_nam_headers)
  target_compile_definitions(ToneDockTests PRIVATE TONEDOCK_NAM_FIXTURES="${nam_SOURCE_DIR}/example_models")
  add_test(NAME tonedock-core COMMAND ToneDockTests)
endif()
