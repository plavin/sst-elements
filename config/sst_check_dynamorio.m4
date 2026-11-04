AC_DEFUN([SST_CHECK_DYNAMORIO],
[
  sst_check_dynamorio_happy="yes"

  AC_ARG_WITH([dynamorio],
    [AS_HELP_STRING([--with-dynamorio@<:@=DIR@:>@],
      [Use the DynamoRIO dynamic instrumentation framework in DIR])])

  AS_IF([test "x$with_dynamorio" = "xno"], [sst_check_dynamorio_happy="no"])

  DYNAMORIO_DIR=""
  DYNAMORIO_RUN=""
  DYNAMORIO_CPPFLAGS=""
  DYNAMORIO_LDFLAGS=""
  DYNAMORIO_ARCH_CPPFLAGS=""

  AS_IF([test -n "$with_dynamorio" -a "$with_dynamorio" != "yes" -a "$with_dynamorio" != "no"],
    [DYNAMORIO_DIR="$with_dynamorio"],
    [AS_IF([test -n "$DYNAMORIO_HOME"], [DYNAMORIO_DIR="$DYNAMORIO_HOME"])])

  dnl DynamoRIO headers require the target architecture to be defined by the client build
  AS_IF([test "$sst_check_dynamorio_happy" = "yes"], [
    AS_CASE([$host_cpu],
      [aarch64|arm64], [DYNAMORIO_ARCH_CPPFLAGS="-DLINUX -DARM_64"],
      [x86_64|amd64],  [DYNAMORIO_ARCH_CPPFLAGS="-DLINUX -DX86_64"],
      [sst_check_dynamorio_happy="no"
       AC_MSG_WARN([DynamoRIO frontend does not support host CPU $host_cpu])])
  ])

  AS_IF([test "$sst_check_dynamorio_happy" = "yes"], [
    AS_IF([test -z "$DYNAMORIO_DIR"], [sst_check_dynamorio_happy="no"])
    AS_IF([test "$sst_check_dynamorio_happy" = "yes"], [
      DYNAMORIO_RUN="$DYNAMORIO_DIR/bin64/drrun"
      AS_IF([test ! -x "$DYNAMORIO_RUN"], [sst_check_dynamorio_happy="no"])

      AS_IF([test ! -d "$DYNAMORIO_DIR/include" -o ! -d "$DYNAMORIO_DIR/ext/include"],
        [sst_check_dynamorio_happy="no"],
        [DYNAMORIO_CPPFLAGS="-I$DYNAMORIO_DIR/include -I$DYNAMORIO_DIR/ext/include"])

      DYNAMORIO_LDFLAGS="-L$DYNAMORIO_DIR/lib64/release -L$DYNAMORIO_DIR/ext/lib64/release"
      for dr_lib in "lib64/release/libdynamorio.so" "ext/lib64/release/libdrmgr.so" \
                    "ext/lib64/release/libdrreg.so" "ext/lib64/release/libdrutil.so" ; do
        AS_IF([test ! -f "$DYNAMORIO_DIR/$dr_lib"],
          [sst_check_dynamorio_happy="no"
           AC_MSG_WARN([DynamoRIO library $DYNAMORIO_DIR/$dr_lib not found])])
      done
    ])
  ])

  dnl Make sure the client headers are usable with the selected architecture
  AS_IF([test "$sst_check_dynamorio_happy" = "yes"], [
    AC_LANG_PUSH([C++])
    CPPFLAGS_saved="$CPPFLAGS"
    CPPFLAGS="$CPPFLAGS $DYNAMORIO_ARCH_CPPFLAGS $DYNAMORIO_CPPFLAGS"
    AC_MSG_CHECKING([whether DynamoRIO client headers compile])
    AC_COMPILE_IFELSE([AC_LANG_PROGRAM([[#include "dr_api.h"
#include "drmgr.h"
#include "drreg.h"
#include "drutil.h"]], [[return (int)sizeof(instr_t*);]])],
      [AC_MSG_RESULT([yes])],
      [AC_MSG_RESULT([no])
       sst_check_dynamorio_happy="no"])
    CPPFLAGS="$CPPFLAGS_saved"
    AC_LANG_POP([C++])
  ])

  AS_IF([test "$sst_check_dynamorio_happy" = "yes"], [], [DYNAMORIO_RUN=""])

  AC_SUBST([DYNAMORIO_DIR])
  AC_SUBST([DYNAMORIO_RUN])
  AC_SUBST([DYNAMORIO_CPPFLAGS])
  AC_SUBST([DYNAMORIO_LDFLAGS])
  AC_SUBST([DYNAMORIO_ARCH_CPPFLAGS])

  AM_CONDITIONAL([HAVE_DYNAMORIO], [test "$sst_check_dynamorio_happy" = "yes"])

  AS_IF([test "$sst_check_dynamorio_happy" = "yes"],
    [AC_DEFINE([HAVE_DYNAMORIO], [1], [Set to 1 if DynamoRIO is available])])
  AC_DEFINE_UNQUOTED([DYNAMORIO_EXECUTABLE], ["$DYNAMORIO_RUN"],
    [Defines the drrun executable used to launch the Ariel DynamoRIO frontend])

  AC_MSG_CHECKING([DynamoRIO])
  AC_MSG_RESULT([$sst_check_dynamorio_happy])

  AS_IF([test "$sst_check_dynamorio_happy" = "no" -a -n "$with_dynamorio" -a "$with_dynamorio" != "no"], [$3])
  AS_IF([test "$sst_check_dynamorio_happy" = "yes"], [$1], [$2])
])
