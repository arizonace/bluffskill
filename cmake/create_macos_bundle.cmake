if(NOT APPLE)
    message(FATAL_ERROR "macOS application bundles can be created only on macOS.")
endif()

foreach(required_variable BUNDLE_PATH BUNDLE_NAME BUNDLE_IDENTIFIER BUNDLE_EXECUTABLE BUNDLE_ICON)
    if(NOT DEFINED ${required_variable} OR "${${required_variable}}" STREQUAL "")
        message(FATAL_ERROR "${required_variable} is required.")
    endif()
endforeach()

if(NOT EXISTS "${BUNDLE_EXECUTABLE}")
    message(FATAL_ERROR "Expected executable was not built: ${BUNDLE_EXECUTABLE}")
endif()
if(NOT EXISTS "${BUNDLE_ICON}")
    message(FATAL_ERROR "Expected icon is missing: ${BUNDLE_ICON}")
endif()

get_filename_component(executable_name "${BUNDLE_EXECUTABLE}" NAME)
get_filename_component(icon_name "${BUNDLE_ICON}" NAME)
set(contents_path "${BUNDLE_PATH}/Contents")
set(macos_path "${contents_path}/MacOS")
set(resources_path "${contents_path}/Resources")

file(REMOVE_RECURSE "${BUNDLE_PATH}")
file(MAKE_DIRECTORY "${macos_path}" "${resources_path}")
file(COPY_FILE "${BUNDLE_EXECUTABLE}" "${macos_path}/${executable_name}")
file(COPY_FILE "${BUNDLE_ICON}" "${resources_path}/${icon_name}")
file(WRITE "${contents_path}/Info.plist" "<?xml version=\"1.0\" encoding=\"UTF-8\"?>
<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" \"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">
<plist version=\"1.0\">
<dict>
  <key>CFBundleDevelopmentRegion</key><string>en</string>
  <key>CFBundleDisplayName</key><string>${BUNDLE_NAME}</string>
  <key>CFBundleExecutable</key><string>${executable_name}</string>
  <key>CFBundleIconFile</key><string>${icon_name}</string>
  <key>CFBundleIdentifier</key><string>${BUNDLE_IDENTIFIER}</string>
  <key>CFBundleInfoDictionaryVersion</key><string>6.0</string>
  <key>CFBundleName</key><string>${BUNDLE_NAME}</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleShortVersionString</key><string>0.1.0</string>
  <key>CFBundleVersion</key><string>0.1.0</string>
  <key>NSHighResolutionCapable</key><true/>
</dict>
</plist>
")

message(STATUS "Created macOS bundle: ${BUNDLE_PATH}")
