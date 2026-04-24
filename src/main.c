/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#include <locale.h>

#include "application.h"

int main(int argc, char *argv[])
{
    setlocale(LC_ALL, "");

    g_autoptr(SilktexApplication) app = silktex_application_new();
    return g_application_run(G_APPLICATION(app), argc, argv);
}
