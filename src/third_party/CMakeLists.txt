# libarchive (BSD), linked into the app rather than shelled out to: ZIP,
# RAR, 7z and TAR downloads, and XP's CAB-compressed I386 files (*.EX_,
# *.DL_ -- MSZIP and LZX; its output matches cabextract's byte for byte on
# explorer.exe, shell32.dll and themeui.dll). XP's install ISO it can't list
# (a root directory record shorter than a sector), so src/app reads ISO 9660
# itself.
#
# Built static from the tarball beside this file, so nothing is needed on the
# host beyond the compression libraries every Linux system has (zlib, bzip2,
# xz), which libarchive links as usual.
include(FetchContent)

# --- libarchive: the reader only ---------------------------------------------------
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
foreach(option ENABLE_TAR ENABLE_CPIO ENABLE_CAT ENABLE_UNZIP ENABLE_TEST ENABLE_INSTALL ENABLE_OPENSSL
               ENABLE_MBEDTLS ENABLE_NETTLE ENABLE_LIBB2 ENABLE_LZ4 ENABLE_LZO ENABLE_ZSTD ENABLE_LIBXML2
               ENABLE_EXPAT ENABLE_PCREPOSIX ENABLE_PCRE2POSIX ENABLE_LIBGCC ENABLE_CNG ENABLE_XATTR ENABLE_ACL
               ENABLE_ICONV)
  set(${option} OFF CACHE BOOL "" FORCE)
endforeach()
foreach(option ENABLE_ZLIB ENABLE_BZip2 ENABLE_LZMA)
  set(${option} ON CACHE BOOL "" FORCE)
endforeach()
FetchContent_Declare(libarchive
  URL ${CMAKE_CURRENT_SOURCE_DIR}/libarchive-3.8.9.tar.xz
  URL_HASH SHA256=888c934f9d95648ecb9163dc8e23ab80a476ecb81a8f1154704a227b5b676dde)
FetchContent_MakeAvailable(libarchive)
# (Its warnings are libarchive's business.)
target_compile_options(archive_static PRIVATE -w)
set_target_properties(archive_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(xplasma::archive ALIAS archive_static)
