/*
 * SilkTex - Modern LaTeX Editor
 * Copyright (C) 2026 Bela Georg Barthelmes
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define SILKTEX_TYPE_APPLICATION (silktex_application_get_type())
G_DECLARE_FINAL_TYPE(SilktexApplication, silktex_application, SILKTEX, APPLICATION, AdwApplication)

SilktexApplication *silktex_application_new(void);

G_END_DECLS
