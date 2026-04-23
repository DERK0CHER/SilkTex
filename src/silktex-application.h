/*
 * SilkTex - Modern LaTeX Editor
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <adwaita.h>

G_BEGIN_DECLS

#define SILKTEX_TYPE_APPLICATION (silktex_application_get_type())
G_DECLARE_FINAL_TYPE(SilktexApplication, silktex_application, SILKTEX, APPLICATION, AdwApplication)

SilktexApplication *silktex_application_new(void);

G_END_DECLS
