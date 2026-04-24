#pragma once

#include <glib.h>

/* Keep translation helpers available without requiring libintl headers. */
#ifndef _
#define _(String) g_dgettext(GETTEXT_PACKAGE, (String))
#endif

#ifndef N_
#define N_(String) (String)
#endif

#ifndef C_
#define C_(Context, String) g_dpgettext2(GETTEXT_PACKAGE, (Context), (String))
#endif
