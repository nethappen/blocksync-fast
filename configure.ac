
# ./configure.ac - this file is a part of program blocksync-fast

# Copyright (C) 2024 Marcin Koczwara <mk@nethorizon.pl>

# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at

#     http://www.apache.org/licenses/LICENSE-2.0

# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.



AC_PREREQ([2.71])
AC_INIT([blocksync-fast], [1.05], [mk@nethorizon.pl])
AM_INIT_AUTOMAKE([-Wall -Werror foreign])
AC_CONFIG_SRCDIR([src/blocksync-fast.c])
AC_CONFIG_HEADERS([config.h])

# Checks for programs.
AC_PROG_CC

# Checks for libraries.
#AC_CHECK_LIB([m], [main])

# Checks for header files.
AC_CHECK_HEADERS([fcntl.h stdint.h sys/param.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_CHECK_HEADER_STDBOOL
AC_TYPE_UINT64_T
AC_TYPE_OFF_T
AC_TYPE_SIZE_T
# Checks for library functions.
AC_FUNC_MALLOC
AC_FUNC_MMAP
AC_FUNC_REALLOC
AC_CHECK_FUNCS([ftruncate getpagesize munmap strcasecmp strerror strtoul])

# Checking for libgcrypt
LIBGCRYPT_MIN_VERSION=1.9.0
AM_PATH_LIBGCRYPT($LIBGCRYPT_MIN_VERSION, HAVE_GCRYPT=yes, HAVE_GCRYPT=no)
if test "x$HAVE_GCRYPT" = "xno"; then
    AC_MSG_ERROR([libgcrypt not found, please install it])
fi

# Check for xxhash
XXHASH_MIN_VERSION=0.8.0
AC_ARG_WITH([xxhash],
  [AS_HELP_STRING([--with-xxhash], [use libxxhash for fast hashing algorithm (auto/yes/no)])],
  [],
  [with_xxhash=auto])

AC_MSG_CHECKING([libxxhash fast hashing algorithm])
AC_MSG_RESULT([$with_xxhash])

if test "x$with_xxhash" != "xno"; then
  PKG_CHECK_MODULES([XXHASH], [libxxhash >= $XXHASH_MIN_VERSION], [have_xxhash=yes], [have_xxhash=no])

  if test "x$have_xxhash" = "xyes"; then
    AC_DEFINE([HAVE_XXHASH], [1], [have xxhash])
    AC_MSG_NOTICE([xxhash fast hashing algorithm library found >= $XXHASH_MIN_VERSION])
    XXHASH_CFLAGS="-DXXH_INLINE_ALL"
    AX_CHECK_X86_FEATURES
  
  elif test "x$with_xxhash" = "xyes"; then
	  AC_MSG_ERROR([xxhash >= $XXHASH_MIN_VERSION not found, install it or build with --with-xxhash=no])
  else
    AC_MSG_NOTICE([Build without xxhash fast hashing algorithm])
  fi
fi

AC_PROG_RANLIB

AC_CONFIG_FILES([
 Makefile
 src/Makefile
])

AC_OUTPUT
