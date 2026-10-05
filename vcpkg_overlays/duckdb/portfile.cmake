vcpkg_from_github(
        OUT_SOURCE_PATH SOURCE_PATH
        REPO duckdb/duckdb
        REF v${VERSION}
        SHA512 ca6866600430b977a970fc3dd83911d719ffae7aae68f39106f3b6667f0b2d481290141f93e0bb35a595357c8d316859b133c08839fd6b1e1cdce7c9fb8b78ac
        HEAD_REF main
    PATCHES
        library-linkage.diff
)
# Remove vendored dependencies which are optional or not properly namespaced
file(REMOVE_RECURSE
    "${SOURCE_PATH}/extension/third_party/icu"
    "${SOURCE_PATH}/third_party/catch"
    "${SOURCE_PATH}/third_party/imdb"
    "${SOURCE_PATH}/third_party/snowball"
    "${SOURCE_PATH}/third_party/tpce-tool"
)

if(VCPKG_CMAKE_SYSTEM_NAME STREQUAL "WASI")
    vcpkg_replace_string("${SOURCE_PATH}/src/main/http/http_util.cpp"
        "#include \"duckdb/common/http_util.hpp\""
        "#include \"duckdb/common/http_util.hpp\"\n\n#include <exception>")
    # wasi-libc's <langinfo.h> defines ERA as a macro, clashing with DatePartSpecifier::ERA.
    vcpkg_replace_string("${SOURCE_PATH}/src/include/duckdb/common/enums/date_part_specifier.hpp"
        "enum class DatePartSpecifier : uint8_t {"
        "#undef ERA\nenum class DatePartSpecifier : uint8_t {")
    # WASI has no terminal size ioctl or mlock.
    vcpkg_replace_string("${SOURCE_PATH}/src/common/printer.cpp"
        "#else\n\tstruct winsize w = {};"
        "#elif defined(__wasi__)\n\treturn 120;\n#else\n\tstruct winsize w = {};")
    vcpkg_replace_string("${SOURCE_PATH}/src/common/encryption_key_manager.cpp"
        "#else\n#include <sys/mman.h>"
        "#elif !defined(__wasi__)\n#include <sys/mman.h>")
    vcpkg_replace_string("${SOURCE_PATH}/src/common/encryption_key_manager.cpp"
        "#elif defined(__MVS__)\n\t__mlockall(_BPX_NONSWAP);"
        "#elif defined(__wasi__)\n#elif defined(__MVS__)\n\t__mlockall(_BPX_NONSWAP);")
    vcpkg_replace_string("${SOURCE_PATH}/src/common/encryption_key_manager.cpp"
        "#elif defined(__MVS__)\n\t__mlockall(_BPX_SWAP);"
        "#elif defined(__wasi__)\n#elif defined(__MVS__)\n\t__mlockall(_BPX_SWAP);")
    vcpkg_replace_string("${SOURCE_PATH}/src/storage/block_allocator.cpp"
        "#elif defined(__MVS__)\n\t// the madvice functionality is not available on z/OS in any form\n\tsuccess = true;"
        "#elif defined(__MVS__) || defined(__wasi__)\n\t// Memory advice is not available on these platforms.\n\tsuccess = true;")
    vcpkg_replace_string("${SOURCE_PATH}/src/parallel/task_scheduler.cpp"
        "#elif defined(_GNU_SOURCE)\n\tauto cpu = sched_getcpu();"
        "#elif defined(_GNU_SOURCE) && !defined(__wasi__)\n\tauto cpu = sched_getcpu();")
    vcpkg_replace_string("${SOURCE_PATH}/third_party/httplib/httplib.hpp"
        "#include <net/if.h>"
        "#ifndef __wasi__\n#include <net/if.h>\n#endif")
    vcpkg_replace_string("${SOURCE_PATH}/third_party/httplib/httplib.hpp"
        "#include <sys/un.h>"
        "#ifndef __wasi__\n#include <sys/un.h>\n#endif")
    vcpkg_replace_string("${SOURCE_PATH}/third_party/httplib/httplib.hpp"
        "#if !defined(_WIN32) || defined(CPPHTTPLIB_HAVE_AFUNIX_H)"
        "#if (!defined(_WIN32) && !defined(__wasi__)) || defined(CPPHTTPLIB_HAVE_AFUNIX_H)")
    vcpkg_replace_string("${SOURCE_PATH}/third_party/httplib/httplib.hpp"
        "#if !defined _WIN32 && !defined ANDROID && !defined _AIX && !defined __MVS__"
        "#if !defined _WIN32 && !defined ANDROID && !defined _AIX && !defined __MVS__ && !defined __wasi__")
    set(WASI_OPTIONS
        -DDISABLE_THREADS=ON
        -DDISABLE_EXTENSION_LOAD=ON
        -DDISABLE_BUILTIN_HTTPLIB=ON
        -DENABLE_EXTENSION_AUTOINSTALL=0
        -DENABLE_EXTENSION_AUTOLOADING=0
        -DENABLE_JEMALLOC=OFF
    )
endif()

set(extension_dirs "")

if("excel" IN_LIST FEATURES)
    vcpkg_from_github(
        OUT_SOURCE_PATH DUCKDB_EXCEL_SOURCE_PATH
        REPO duckdb/duckdb-excel
        REF 9421a2d75bd7544336caa73e5f9e6063cc7f6992
        SHA512 0532844ba531d70e78c499ce7caefd772da4b013459676da1b552b20c3ac674af979b1958d84566147580cd39cf97ba4fefc010bcfb5fcb70628deaf2e95dbb0
        HEAD_REF main
        PATCHES
            library-linkage-excel.diff
    )
    list(APPEND extension_dirs "${DUCKDB_EXCEL_SOURCE_PATH}")
    file(WRITE "${SOURCE_PATH}/.github/config/extensions/excel.cmake" "
duckdb_extension_load(excel
    SOURCE_DIR \"${DUCKDB_EXCEL_SOURCE_PATH}\"
    INCLUDE_DIR \"${DUCKDB_EXCEL_SOURCE_PATH}/src/excel/include\"
)
")
endif()

if("httpfs" IN_LIST FEATURES)
    vcpkg_from_github(
        OUT_SOURCE_PATH DUCKDB_HTTPFS_SOURCE_PATH
        REPO duckdb/duckdb-httpfs
        REF 4bc690dba4496c765777a0269d48fdbaff7cdc11
        SHA512 25263a97034cd28a4f6de9333e6c704c8c998851b7fe7b1558346faf9c958e69b9d8fbb41a007803d7230fc0a57fa714098d887a3e62b5726b6106d23b413263
        HEAD_REF main
        PATCHES
            library-linkage-httpfs.diff
    )
    list(APPEND extension_dirs "${DUCKDB_HTTPFS_SOURCE_PATH}")
    file(WRITE "${SOURCE_PATH}/.github/config/extensions/httpfs.cmake" "
duckdb_extension_load(httpfs
    SOURCE_DIR \"${DUCKDB_HTTPFS_SOURCE_PATH}\"
    INCLUDE_DIR \"${DUCKDB_HTTPFS_SOURCE_PATH}/src/include\"
)
")
endif()

if("iceberg" IN_LIST FEATURES)
    vcpkg_from_github(
        OUT_SOURCE_PATH DUCKDB_ICEBERG_SOURCE_PATH
        REPO duckdb/duckdb-iceberg
        REF 6cec0127c340bc7e83c7e6b2390e27cb555a9d0a
        SHA512 d49f7e9f0492111ac81f0c34db84ca675c6a39fb66f257d080953a53922243d6cff09ee1c1dbb0f5fefc911b936ed8b03df2b30431cd6e2319c4cbefcf8690b6
        HEAD_REF main
    )
    list(APPEND extension_dirs "${DUCKDB_ICEBERG_SOURCE_PATH}")
    file(WRITE "${SOURCE_PATH}/.github/config/extensions/iceberg.cmake" "
duckdb_extension_load(iceberg
    SOURCE_DIR \"${DUCKDB_ICEBERG_SOURCE_PATH}\"
    INCLUDE_DIR \"${DUCKDB_ICEBERG_SOURCE_PATH}/src/include\"
)
")
endif()

set(BUILD_EXTENSIONS "${FEATURES}")
list(FILTER BUILD_EXTENSIONS INCLUDE REGEX "^(autocomplete|excel|httpfs|icu|json|tpcds|tpch)\$")

string(COMPARE EQUAL "${VCPKG_LIBRARY_LINKAGE}" "static" EXTENSION_STATIC_BUILD)

if(VCPKG_CROSSCOMPILING AND NOT DEFINED DUCKDB_EXPLICIT_PLATFORM)
    set(DUCKDB_EXPLICIT_PLATFORM "")
    if(VCPKG_TARGET_ARCHITECTURE STREQUAL "arm64")
        set(DUCKDB_EXPLICIT_PLATFORM "arm64")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x64")
        set(DUCKDB_EXPLICIT_PLATFORM "amd64")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "x86")
        set(DUCKDB_EXPLICIT_PLATFORM "i686")
    elseif(VCPKG_TARGET_ARCHITECTURE STREQUAL "wasm32")
        set(DUCKDB_EXPLICIT_PLATFORM "wasm_eh")
    endif()
    if(DUCKDB_EXPLICIT_PLATFORM)
        if(VCPKG_TARGET_IS_ANDROID)
            string(APPEND DUCKDB_EXPLICIT_PLATFORM "-linux_android")
        elseif(VCPKG_TARGET_IS_FREEBSD)
            string(APPEND DUCKDB_EXPLICIT_PLATFORM "-freebsd")
        elseif(VCPKG_TARGET_IS_LINUX)
            string(APPEND DUCKDB_EXPLICIT_PLATFORM "-linux")
        elseif(VCPKG_TARGET_IS_OSX)
            string(APPEND DUCKDB_EXPLICIT_PLATFORM "-osx")
        elseif(VCPKG_TARGET_IS_WINDOWS)
            string(APPEND DUCKDB_EXPLICIT_PLATFORM "-windows")
            if(VCPKG_TARGET_IS_MINGW)
                string(APPEND DUCKDB_EXPLICIT_PLATFORM "_mingw")
            endif()
        elseif()
            set(DUCKDB_EXPLICIT_PLATFORM "") # unknown. override in triplet file.
        endif()
    endif()
endif()

vcpkg_cmake_configure(
        SOURCE_PATH ${SOURCE_PATH}
        OPTIONS
            -DOVERRIDE_GIT_DESCRIBE=v${VERSION}-0-g0123456789
            -DDUCKDB_EXPLICIT_PLATFORM=${DUCKDB_EXPLICIT_PLATFORM}
            -DDUCKDB_EXPLICIT_VERSION=v${VERSION}
            "-DBUILD_EXTENSIONS=${BUILD_EXTENSIONS}"
            "-DSKIP_EXTENSIONS=core_functions"
            -DBUILD_SHELL=FALSE
            -DBUILD_UNITTESTS=OFF
            -DCMAKE_CXX_STANDARD=17
            -DCMAKE_JOB_POOL_LINK=console # Serialize linking to avoid OOM
            -DENABLE_EXTENSION_AUTOINSTALL=1
            -DENABLE_EXTENSION_AUTOLOADING=1
            -DENABLE_SANITIZER=OFF
            -DENABLE_THREAD_SANITIZER=OFF
            -DENABLE_UBSAN=OFF
            "-DEXTENSION_CONFIG_BASE_DIR=${SOURCE_PATH}/OUT_OF_TREE"
            "-DEXTENSION_STATIC_BUILD=${EXTENSION_STATIC_BUILD}"
            "-DINSTALL_CMAKE_DIR:STRING=share/${PORT}"
            -DWITH_INTERNAL_ICU=OFF
            ${WASI_OPTIONS}
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup()

# Custom HTTPUtil implementations must derive from HTTPFSUtil: httpfs casts params to HTTPFSParams.
if("httpfs" IN_LIST FEATURES)
    file(INSTALL "${DUCKDB_HTTPFS_SOURCE_PATH}/src/include/httpfs_client.hpp" DESTINATION "${CURRENT_PACKAGES_DIR}/include/duckdb/httpfs")
endif()

if(VCPKG_LIBRARY_LINKAGE STREQUAL "static")
    foreach(path IN ITEMS duckdb.h duckdb/common/winapi.hpp)
        vcpkg_replace_string("${CURRENT_PACKAGES_DIR}/include/${path}" "#ifdef DUCKDB_STATIC_BUILD" "#if 1")
    endforeach()
endif()

file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/include")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/debug/share")
# empty dirs
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/duckdb/main/capi/header_generation")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/include/duckdb/storage/serialization")

file(INSTALL "${CMAKE_CURRENT_LIST_DIR}/usage" DESTINATION "${CURRENT_PACKAGES_DIR}/share/${PORT}")

set(third_party_licenses "")
file(COPY_FILE "${SOURCE_PATH}/third_party/thrift/thrift/LICENSE" "${SOURCE_PATH}/third_party/thrift/LICENSE")
file(GLOB third_party_files "${SOURCE_PATH}/third_party/*")
foreach(maybe_directory IN LISTS extension_dirs third_party_files)
    if(IS_DIRECTORY "${maybe_directory}")
        cmake_path(GET maybe_directory FILENAME package)
        set(license_file "${CURRENT_BUILDTREES_DIR}/${TARGET_TRIPLET}-rel/## ${package} license")
        file(COPY_FILE "${maybe_directory}/LICENSE" "${license_file}")
        list(APPEND third_party_licenses "${license_file}")
    endif()
endforeach()
vcpkg_install_copyright(
    COMMENT [[
Duckdb contains copies of many third-party packages which are subject to
separate license terms.
]]
    FILE_LIST
        "${SOURCE_PATH}/LICENSE"
        ${third_party_licenses}
)
