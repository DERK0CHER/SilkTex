/*
 * SilkTex - Modern LaTeX Editor
 * SPDX-License-Identifier: MIT
 */

#include <glib/gi18n.h>
#include <locale.h>

#include "silktex-application.h"

int
main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");
    bindtextdomain("silktex", NULL);
    textdomain("silktex");

    g_autoptr(SilktexApplication) app = silktex_application_new();
    return g_application_run(G_APPLICATION(app), argc, argv);
}
