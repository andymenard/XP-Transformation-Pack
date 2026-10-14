# SPDX-License-Identifier: GPL-3.0-or-later
# libarchive (BSD), linked into the app rather than shelled out to: ZIP,
# RAR, 7z and TAR downloads, and XP's CAB-compressed I386 files (*.EX_,
# *.DL_ -- MSZIP and LZX; its output matches cabextract's byte for byte on
# explorer.exe, shell32.dll and themeui.dll). XP's install ISO it can't list
# (a root directory record shorter than a sector), so src/app reads ISO 9660
# itself.
#
# Built static from the tarball beside this file, so nothing is needed on the
# host beyond the compression libraries every Linux system has (zlib, xz),
# which libarchive links as usual -- and bzip2, linked in static where the
# build system has it (the release image does): its library's file name
# differs between distributions (Debian's libbz2.so.1.0, Fedora's
# libbz2.so.1), so a binary asking for one doesn't start on the others.
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
find_library(XPLASMA_BZIP2_STATIC NAMES libbz2.a)
if(XPLASMA_BZIP2_STATIC)
  set(BZIP2_LIBRARY_RELEASE ${XPLASMA_BZIP2_STATIC} CACHE FILEPATH "" FORCE)
  set(BZIP2_LIBRARY ${XPLASMA_BZIP2_STATIC} CACHE FILEPATH "" FORCE)
endif()
FetchContent_Declare(libarchive
  URL ${CMAKE_CURRENT_SOURCE_DIR}/libarchive-3.8.9.tar.xz
  URL_HASH SHA256=888c934f9d95648ecb9163dc8e23ab80a476ecb81a8f1154704a227b5b676dde
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
FetchContent_MakeAvailable(libarchive)
# (Its warnings are libarchive's business.)
target_compile_options(archive_static PRIVATE -w)
set_target_properties(archive_static PROPERTIES POSITION_INDEPENDENT_CODE ON)
add_library(xplasma::archive ALIAS archive_static)
