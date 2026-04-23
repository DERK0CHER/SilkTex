/*
 * SilkTex - Modern LaTeX Editor
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <glib-object.h>
#include "silktex-editor.h"

G_BEGIN_DECLS

#define SILKTEX_TYPE_COMPILER (silktex_compiler_get_type())
G_DECLARE_FINAL_TYPE(SilktexCompiler, silktex_compiler, SILKTEX, COMPILER, GObject)

SilktexCompiler *silktex_compiler_new(void);

void silktex_compiler_set_typesetter(SilktexCompiler *self, const char *typesetter);
const char *silktex_compiler_get_typesetter(SilktexCompiler *self);

void silktex_compiler_start(SilktexCompiler *self);
void silktex_compiler_stop(SilktexCompiler *self);
void silktex_compiler_pause(SilktexCompiler *self);
void silktex_compiler_resume(SilktexCompiler *self);

void silktex_compiler_request_compile(SilktexCompiler *self, SilktexEditor *editor);
void silktex_compiler_force_compile(SilktexCompiler *self, SilktexEditor *editor);
void silktex_compiler_cancel(SilktexCompiler *self);

gboolean silktex_compiler_is_running(SilktexCompiler *self);
gboolean silktex_compiler_is_compiling(SilktexCompiler *self);

void silktex_compiler_set_shell_escape(SilktexCompiler *self, gboolean enabled);
void silktex_compiler_set_synctex(SilktexCompiler *self, gboolean enabled);
void silktex_compiler_apply_config(SilktexCompiler *self);

const char *silktex_compiler_get_log(SilktexCompiler *self);
int *silktex_compiler_get_error_lines(SilktexCompiler *self);

gboolean silktex_compiler_run_makeindex(SilktexCompiler *self, SilktexEditor *editor);
gboolean silktex_compiler_run_bibtex(SilktexCompiler *self, SilktexEditor *editor);

G_END_DECLS
