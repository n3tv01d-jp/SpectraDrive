# Applied to JUCE by FetchContent's PATCH_COMMAND (run with -DJUCE_DIR=<juce source dir>).
# Each patch is skipped if its marker is already present, so the script is safe to re-run.

function(spectradrive_patch file marker old new description)
    file(READ "${file}" src)

    string(FIND "${src}" "${marker}" already)
    if(NOT already EQUAL -1)
        return()
    endif()

    # JUCE's sources have CRLF line endings when checked out on Windows; match on LF only.
    string(REPLACE "\r\n" "\n" src "${src}")

    string(FIND "${src}" "${old}" found)
    if(found EQUAL -1)
        message(FATAL_ERROR "PatchJuceWine: expected code not found in ${file}. "
                            "JUCE changed - update cmake/PatchJuceWine.cmake for this JUCE version.")
    endif()

    string(REPLACE "${old}" "${new}" src "${src}")
    file(WRITE "${file}" "${src}")
    message(STATUS "PatchJuceWine: ${description}")
endfunction()

# 1) Software renderer under Wine.
#
# JUCE 8 creates every Windows plugin window with the Direct2D renderer, and it builds the
# Direct2D swapchain inside the window's constructor. Under Wine/Proton (e.g. FL Studio in
# Lutris) that swapchain never shows anything, and once it has been attached to the window,
# switching to the software renderer later doesn't help - the window stays black.
# This makes JUCE pick the software (GDI) renderer at window creation when running under
# Wine, detected via ntdll's wine_get_version export. Real Windows keeps Direct2D.
spectradrive_patch(
    "${JUCE_DIR}/modules/juce_gui_basics/native/juce_Windowing_windows.cpp"
    "SPECTRADRIVE_WINE_PATCH"
    "return new HWNDComponentPeer { *this, styleFlags, (HWND) parentHWND, false, 1 };"
    "/* SPECTRADRIVE_WINE_PATCH */\n    const int engine = GetProcAddress (GetModuleHandleW (L\"ntdll.dll\"), \"wine_get_version\") != nullptr ? 0 : 1;\n    return new HWNDComponentPeer { *this, styleFlags, (HWND) parentHWND, false, engine };"
    "JUCE uses the software renderer under Wine")

# 2) Out-of-bounds read when a text run shapes to zero glyphs.
#
# JUCE 8.0.4's line breaker walks backwards from glyphs.cend() without checking for an empty
# glyph list. That happens when a font fails to load (under Wine, JUCE asks for "Bitstream
# Vera Sans", which most prefixes don't have), and it crashes the host with an access
# violation reading address 0xFFFFFFFFFFFFFFE9. Later JUCE versions rewrote this code.
spectradrive_patch(
    "${JUCE_DIR}/modules/juce_graphics/fonts/juce_SimpleShapedText.cpp"
    "SPECTRADRIVE_EMPTY_GLYPHS_PATCH"
    "                        auto it = glyphs.cend();\n\n                        while (--it != glyphs.begin())"
    "                        /* SPECTRADRIVE_EMPTY_GLYPHS_PATCH */\n                        if (glyphs.empty())\n                            return glyphs.cend();\n\n                        auto it = glyphs.cend();\n\n                        while (--it != glyphs.begin())"
    "JUCE text shaping guards against empty glyph runs")
