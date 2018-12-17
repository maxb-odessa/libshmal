#                                               -*- Autoconf -*-
# Process this file with autoconf to produce a configure script.

AC_PREREQ(2.61)
AC_INIT([libshmal], [0.2], [http://libshmal.sf.net])
AC_CONFIG_SRCDIR([shmal.h])
AC_CONFIG_HEADER([config.h])
AC_CONFIG_MACRO_DIR([m4])
AM_INIT_AUTOMAKE([libshmal], [0.2])

AC_CANONICAL_HOST
AC_LANG([C])

# Checks for programs.
AC_PROG_CC
AC_PROG_INSTALL
AC_PROG_LIBTOOL

# args
AC_MSG_CHECKING([for shmem mechanism to use])
AC_ARG_WITH([mechanism],
        [  --with-mechanism=MECH   use 'sysvipc' or 'mmap' for mem allocation (default is 'mmap')],
        [ MECH="$withval" ],
        [ MECH="mmap" ])
case $MECH in
  sysvipc) AC_DEFINE([USE_SYSVIPC], [1], [use SysC IPC])
           AC_MSG_RESULT([sysvipc])
           ;;
     mmap) AC_DEFINE([USE_MMAP], [1], [use mmap])
           AC_MSG_RESULT([mmap]) 
           ;;
        *) AC_ERROR([Please specify one of --with-mechanism=sysvipc or --with-mechanism=mmap])
           ;;
esac

# Checks for libraries.
AC_CHECK_LIB([pthread], [pthread_create])

# Checks for header files.
AC_HEADER_STDC
AC_CHECK_HEADERS([fcntl.h stdlib.h string.h unistd.h])

# Checks for typedefs, structures, and compiler characteristics.
AC_TYPE_OFF_T
AC_TYPE_SIZE_T

# Checks for library functions.
AC_FUNC_MMAP
AC_CHECK_FUNCS([bzero munmap shmget])

AC_CONFIG_FILES([Makefile])

AC_OUTPUT

