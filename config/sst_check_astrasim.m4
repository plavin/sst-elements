
AC_DEFUN([SST_CHECK_ASTRASIM], [
  AC_ARG_WITH([astrasim],
    [AS_HELP_STRING([--with-astrasim@<:@=DIR@:>@],
      [Use ASTRA-sim installed in optionally specified DIR])])

  sst_check_astrasim_happy="yes"
  AS_IF([test "$with_astrasim" = "no"], [sst_check_astrasim_happy="no"])

  CXXFLAGS_saved="$CXXFLAGS"
  CPPFLAGS_saved="$CPPFLAGS"
  LDFLAGS_saved="$LDFLAGS"
  LIBS_saved="$LIBS"

  AS_IF([test ! -z "$with_astrasim" -a "$with_astrasim" != "yes"],
    [ASTRASIM_CPPFLAGS="-I$with_astrasim/include -DHAVE_ASTRASIM"
     CPPFLAGS="$ASTRASIM_CPPFLAGS $AM_CPPFLAGS $CPPFLAGS"
     CXXFLAGS="$AM_CXXFLAGS $CXXFLAGS"
     ASTRASIM_LDFLAGS="-L$with_astrasim/lib64 -Wl,-rpath,$with_astrasim/lib64"
     ASTRASIM_LIBDIR="$with_astrasim/lib64"
     LIBS="-lAstraSim -lstdc++fs"
     LDFLAGS="$ASTRASIM_LDFLAGS $AM_LDFLAGS $LDFLAGS"],
    [ASTRASIM_CPPFLAGS=
     ASTRASIM_LDFLAGS=
     ASTRASIM_LIBS=
     ASTRASIM_LIBDIR=])

  AC_LANG_PUSH(C++)
dnl  AC_CHECK_HEADERS([astra-sim/common/Logging.hh],
dnl                   [], 
dnl                   [sst_check_astrasim_happy="no"])

  AC_LINK_IFELSE(
    [AC_LANG_PROGRAM(
       [[#include <astra-sim/common/Logging.hh>]],
       [[   //write the program out to avoid issues with mangling
            AstraSim::LoggerFactory::init();
       ]]
    )],
    [ASTRASIM_LIBS="$LIBS"],
    [sst_check_astrasim_happy="no"]
  )
  AC_LANG_POP(C++)

  CXXFLAGS="$CXXFLAGS_saved"
  CPPFLAGS="$CPPFLAGS_saved"
  LDFLAGS="$LDFLAGS_saved"
  LIBS="$LIBS_saved"

  AC_SUBST([ASTRASIM_CPPFLAGS])
  AC_SUBST([ASTRASIM_LDFLAGS])
  AC_SUBST([ASTRASIM_LIBS])
  AC_SUBST([ASTRASIM_LIBDIR])
  AM_CONDITIONAL([HAVE_ASTRASIM], [test "$sst_check_astrasim_happy" = "yes"])
  AS_IF([test "$sst_check_astrasim_happy" = "yes"],
        [AC_DEFINE([HAVE_ASTRASIM], [1], [Set to 1 if ASTRA-sim was found])])
  AC_DEFINE_UNQUOTED([ASTRASIM_LIBDIR], ["$ASTRASIM_LIBDIR"], [Path to ASTRA-sim library])

  AS_IF([test "$sst_check_astrasim_happy" = "yes"], [$1], [$2])
])
