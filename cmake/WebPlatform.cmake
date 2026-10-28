# Browser UI/GL calls are proxied; blocking guest work runs in pthread workers.
if(NOT EMSCRIPTEN)
    return()
endif()
add_compile_options(-pthread -msimd128 -msse4.1 "$<$<COMPILE_LANGUAGE:CXX>:-fwasm-exceptions>")
