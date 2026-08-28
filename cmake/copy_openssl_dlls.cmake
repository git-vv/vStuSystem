# copy_openssl_dlls.cmake
# Copy OpenSSL runtime DLLs to the executable output directory so the
# installer can bundle them.  The DLLs are located relative to the
# OpenSSL library paths that CMake already resolved, ensuring we always
# ship the same version the executable was linked against.

if(NOT EXISTS "${TARGET_EXE}")
    message(STATUS "copy_openssl_dlls: target exe not found, skipping")
    return()
endif()

# Derive a search root from the OpenSSL library directory.
# Walk up 1-3 levels to cover common layouts:
#   vcpkg:  .../installed/x64-windows/lib  -> bin/ is one level up
#   binary: .../OpenSSL/lib/VC/x64/MT      -> bin/ is three levels up
get_filename_component(_lib_dir "${OPENSSL_LIB}" DIRECTORY)
set(_dirs "${_lib_dir}")
get_filename_component(_d1 "${_lib_dir}" DIRECTORY)
list(APPEND _dirs "${_d1}")
get_filename_component(_d2 "${_d1}" DIRECTORY)
list(APPEND _dirs "${_d2}")
get_filename_component(_d3 "${_d2}" DIRECTORY)
list(APPEND _dirs "${_d3}")

set(_dll_dir "")
foreach(_d ${_dirs})
    if(EXISTS "${_d}/bin")
        file(GLOB _test_dlls "${_d}/bin/libssl*.dll" "${_d}/bin/libcrypto*.dll"
             "${_d}/bin/openssl*.dll" "${_d}/bin/ssleay*.dll" "${_d}/bin/libeay*.dll")
        if(_test_dlls)
            set(_dll_dir "${_d}/bin")
            break()
        endif()
    endif()
    file(GLOB _test_dlls "${_d}/libssl*.dll" "${_d}/libcrypto*.dll"
         "${_d}/openssl*.dll" "${_d}/ssleay*.dll" "${_d}/libeay*.dll")
    if(_test_dlls)
        set(_dll_dir "${_d}")
        break()
    endif()
endforeach()

if(NOT _dll_dir)
    message(WARNING "copy_openssl_dlls: could not locate OpenSSL DLLs. "
            "Searched: ${_dirs}")
    return()
endif()

message(STATUS "copy_openssl_dlls: found DLLs in ${_dll_dir}")

file(GLOB _dlls
    "${_dll_dir}/libssl*.dll"
    "${_dll_dir}/libcrypto*.dll"
    "${_dll_dir}/openssl*.dll"
    "${_dll_dir}/ssleay*.dll"
    "${_dll_dir}/libeay*.dll"
)

foreach(_dll ${_dlls})
    get_filename_component(_name "${_dll}" NAME)
    file(COPY "${_dll}" DESTINATION "${OUTPUT_DIR}")
    message(STATUS "copy_openssl_dlls: copied ${_name} -> ${OUTPUT_DIR}")
endforeach()

if(NOT _dlls)
    message(WARNING "copy_openssl_dlls: no DLL files found in ${_dll_dir}")
endif()
