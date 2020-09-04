AC_DEFUN([AX_SHENANGO],[
tryshenangodir=""
AC_ARG_WITH(shenango,
       [  --with-shenango=PATH     Specify path to shenango installation ],
       [
                if test "x$withval" != "xno" ; then
                        tryshenangodir=$withval
                fi
       ]
)

dnl ------------------------------------------------------

AC_CACHE_CHECK([for shenango directory], ac_cv_shenango_dir, [
  saved_LIBS="$LIBS"
  saved_LDFLAGS="$LDFLAGS"
  saved_CPPFLAGS="$CPPFLAGS"
  le_found=no
  for ledir in $tryshenangodir "" $prefix /usr/local ; do
    RUNTIME_LIBS=$(make -f $ledir/Makefile print-RUNTIME_LIBS ROOT_PATH=$ledir | grep RUNTIME_LIBS | sed 's/.*= //g')

    LDFLAGS="$saved_LDFLAGS"
    LIBS="$RUNTIME_LIBS $saved_LIBS"

    # Skip the directory if it isn't there.
    if test ! -z "$ledir" -a ! -d "$ledir" ; then
       continue;
    fi
    if test ! -z "$ledir" ; then
        LDFLAGS="-L$ledir -T $ledir/base/base.ld $LDFLAGS"
        CPPFLAGS="-I$ledir/inc $CPPFLAGS"
    fi
    # Can I compile and link it?
    AC_LINK_IFELSE([AC_LANG_PROGRAM([[#include <sys/time.h>
#include <sys/types.h>
#include <runtime/thread.h>]], [[ thread_yield() ]])],[ shenango_linked=yes ],[ shenango_linked=no ])
    if test $shenango_linked = yes; then
       if test ! -z "$ledir" ; then
         ac_cv_shenango_dir=$ledir
       else
         ac_cv_shenango_dir="(system)"
       fi
       le_found=yes
       break
    fi
  done
  LIBS="$saved_LIBS"
  LDFLAGS="$saved_LDFLAGS"
  CPPFLAGS="$saved_CPPFLAGS"
  if test $le_found = no ; then
    AC_MSG_ERROR([shenango is required. 

      If it's already installed, specify its path using --with-shenango=/dir/
])
  fi
])
SHEN_LIBS="$ac_cv_shenango_dir/shim/mem.o $RUNTIME_LIBS -ldl"
if test $ac_cv_shenango_dir != "(system)"; then
    SHEN_LDFLAGS="-no-pie -L$ac_cv_shenango_dir -T $ac_cv_shenango_dir/base/base.ld"
    le_libdir="$ac_cv_shenango_dir"
    SHEN_CPPFLAGS="-I$ac_cv_shenango_dir/inc"
fi
SHEN_CPPFLAGS="-DNDEBUG -O3 -Wall -std=gnu11 -D_GNU_SOURCE -mssse3 $SHEN_CPPFLAGS"

])dnl AX_SHENANGO
