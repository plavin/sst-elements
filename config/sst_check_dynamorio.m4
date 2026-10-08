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

  AS_IF([test -n "$with_dynamorio" -a "$with_dynamorio" != "yes" -a "$with_dynamorio" != "no"],
    [DYNAMORIO_DIR="$with_dynamorio"],
    [AS_IF([test -n "$DYNAMORIO_HOME"], [DYNAMORIO_DIR="$DYNAMORIO_HOME"])])

  AS_IF([test "$sst_check_dynamorio_happy" = "yes"], [
    AS_IF([test -z "$DYNAMORIO_DIR"], [sst_check_dynamorio_happy="no"])
    AS_IF([test "$sst_check_dynamorio_happy" = "yes"], [
      DYNAMORIO_RUN="$DYNAMORIO_DIR/bin64/drrun"
      AS_IF([test ! -x "$DYNAMORIO_RUN"], [sst_check_dynamorio_happy="no"])

      AS_IF([test ! -d "$DYNAMORIO_DIR/include" -o ! -d "$DYNAMORIO_DIR/ext/include"],
        [sst_check_dynamorio_happy="no"],
        [DYNAMORIO_CPPFLAGS="-I$DYNAMORIO_DIR/include -I$DYNAMORIO_DIR/ext/include"])

      AS_IF([test -d "$DYNAMORIO_DIR/lib64/release"],
        [DYNAMORIO_LDFLAGS="$DYNAMORIO_LDFLAGS -L$DYNAMORIO_DIR/lib64/release"])
      AS_IF([test -d "$DYNAMORIO_DIR/ext/lib64/release"],
        [DYNAMORIO_LDFLAGS="$DYNAMORIO_LDFLAGS -L$DYNAMORIO_DIR/ext/lib64/release"])
      AS_IF([test -d "$DYNAMORIO_DIR/lib32/release"],
        [DYNAMORIO_LDFLAGS="$DYNAMORIO_LDFLAGS -L$DYNAMORIO_DIR/lib32/release"])
      AS_IF([test -d "$DYNAMORIO_DIR/ext/lib32/release"],
        [DYNAMORIO_LDFLAGS="$DYNAMORIO_LDFLAGS -L$DYNAMORIO_DIR/ext/lib32/release"])
    ])
  ])

  AC_SUBST([DYNAMORIO_DIR])
  AC_SUBST([DYNAMORIO_RUN])
  AC_SUBST([DYNAMORIO_CPPFLAGS])
  AC_SUBST([DYNAMORIO_LDFLAGS])

  AM_CONDITIONAL([HAVE_DYNAMORIO], [test "$sst_check_dynamorio_happy" = "yes"])

  AS_IF([test "$sst_check_dynamorio_happy" = "yes"],
    [AC_DEFINE([HAVE_DYNAMORIO], [1], [Set to 1 if DynamoRIO is available])])

  AC_MSG_CHECKING([DynamoRIO])
  AC_MSG_RESULT([$sst_check_dynamorio_happy])

  AS_IF([test "$sst_check_dynamorio_happy" = "no" -a -n "$with_dynamorio" -a "$with_dynamorio" != "no"], [$3])
  AS_IF([test "$sst_check_dynamorio_happy" = "yes"], [$1], [$2])
])
