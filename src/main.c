/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * Program entry: set the locale for gettext and GTK, then hand off to
 * AdwApplication (see application.c). No command-line handling here —
 * GApplication parses argv (e.g. opening files) in the application class.
 */

#include <locale.h>

#include "application.h"

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    g_autoptr(SilktexApplication) app = silktex_application_new();
    return g_application_run(G_APPLICATION(app), argc, argv);
}
