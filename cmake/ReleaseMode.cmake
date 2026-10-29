include(CheckIPOSupported)

function(EnableFastReleaseMode TargetName)
    # Portable library/test-only configurations do not use this function and
    # should not compile/link an unrelated IPO probe (especially across WSL I/O).
    get_property(ipo_checked GLOBAL PROPERTY DRAKAN_IPO_SUPPORTED SET)
    if(NOT ipo_checked)
        check_ipo_supported(RESULT IPO_SUPPORTED OUTPUT IPO_ERROR)
        set_property(GLOBAL PROPERTY DRAKAN_IPO_SUPPORTED "${IPO_SUPPORTED}")
        set_property(GLOBAL PROPERTY DRAKAN_IPO_ERROR "${IPO_ERROR}")
    endif()
    get_property(IPO_SUPPORTED GLOBAL PROPERTY DRAKAN_IPO_SUPPORTED)
    get_property(IPO_ERROR GLOBAL PROPERTY DRAKAN_IPO_ERROR)
    message("> Enabling optimization for: ${TargetName}")
    if(MSVC)
        target_compile_options(${TargetName} PRIVATE
            $<$<CONFIG:Release>:
                /O2 # speed
                /Ob2 # inline aggressively
                /Oi # intrinsics
                /GL # whole program opt
                /Gy # function-level linking
                /Gw # global data in COMDAT
                /GF # string pooling
                /Zc:inline # remove unreferenced inline
                /fp:fast # fast math (graphics friendly)
                /DNDEBUG
                /arch:AVX2 # Advanced Vector Extensions 2
                /GS- # Disable Buffer Security Check (faster)
                /Qspectre- # Disable Spectre mitigations (faster)
            >
        )

        if(TARGET ${TargetName})
            target_link_options(${TargetName} PRIVATE
                $<$<CONFIG:Release>:
                    /LTCG # link-time code generation
                    /OPT:REF # remove unreferenced
                    /OPT:ICF # fold identical COMDATs
                >
            )
        endif()
    endif()

    if(IPO_SUPPORTED)
        set_property(TARGET ${TargetName} PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
        # Clang's ThinLTO through GNU ld's plugin drops inline template definitions it chose from bitcode when native
        # (non-LTO) libraries linked after it reference them; lld resolves mixed bitcode / native links correctly.
        if(CMAKE_CXX_COMPILER_ID STREQUAL "Clang" AND NOT EMSCRIPTEN AND NOT WIN32)
            get_property(lld_checked GLOBAL PROPERTY DRAKAN_HAVE_LLD SET)
            if(NOT lld_checked)
                include(CheckLinkerFlag)
                check_linker_flag(CXX "-fuse-ld=lld" DRAKAN_LLD_WORKS)
                set_property(GLOBAL PROPERTY DRAKAN_HAVE_LLD "${DRAKAN_LLD_WORKS}")
            endif()
            get_property(have_lld GLOBAL PROPERTY DRAKAN_HAVE_LLD)
            if(have_lld)
                target_link_options(${TargetName} PRIVATE $<$<CONFIG:Release>:-fuse-ld=lld>)
            endif()
        endif()
    else()
        message(WARNING "Interprocedural optimization not supported: ${IPO_ERROR}")
    endif()
endfunction()
