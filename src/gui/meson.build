
source_files = files(
    'biblio.c',
    'biblio.h',
    'compile/latexmk.c',
    'compile/latexmk.h',
    'compile/rubber.c',
    'compile/rubber.h',
    'compile/texlive.c',
    'compile/texlive.h',
    'configfile.c',
    'configfile.h',
    'constants.h',
    'editor.c',
    'editor.h',
    'environment.c',
    'environment.h',
    'external.c',
    'external.h',
    'gui/gui-import.c',
    'gui/gui-import.h',
    'gui/gui-infoscreen.c',
    'gui/gui-infoscreen.h',
    'gui/gui-main.c',
    'gui/gui-main.h',
    'gui/gui-menu.c',
    'gui/gui-menu.h',
    'gui/gui-prefs.c',
    'gui/gui-prefs.h',
    'gui/gui-preview.c',
    'gui/gui-preview.h',
    'gui/gui-project.c',
    'gui/gui-project.h',
    'gui/gui-search.c',
    'gui/gui-search.h',
    'gui/gui-snippets.c',
    'gui/gui-snippets.h',
    'gui/gui-tabmanager.c',
    'gui/gui-tabmanager.h',
    'importer.c',
    'importer.h',
    'iofunctions.c',
    'iofunctions.h',
    'latex.c',
    'latex.h',
    'main.c',
    'motion.c',
    'motion.h',
    'project.c',
    'project.h',
    'signals.c',
    'signals.h',
    'snippets.c',
    'snippets.h',
    'tabmanager.c',
    'tabmanager.h',
    'template.c',
    'template.h',
    'utils.c',
    'utils.h',
)



project_target = executable(
  meson.project_name(),
  source_files,
  dependencies: project_dependencies,
  install : true,
  link_args: '-rdynamic',
  c_args : build_args,
)

