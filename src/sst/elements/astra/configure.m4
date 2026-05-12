dnl -*- Autoconf -*-

AC_DEFUN([SST_astra_CONFIG], [
  sst_check_astrax="yes"

  SST_CHECK_ASTRASIM([have_astrasim=1],[have_astrasim=0],[AC_MSG_ERROR([ASTRA-sim required, but not found])])

  AS_IF([test "$have_astrasim" = 1], [sst_check_astrax="yes"], [sst_check_astrax="no"])
  AS_IF([test "$sst_check_astrax" = "yes"], [$1], [$2])
])

