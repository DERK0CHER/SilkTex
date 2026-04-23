silktex_sources = files(
    'silktex-application.c',
    'silktex-application.h',
    'silktex-window.c',
    'silktex-window.h',
    'silktex-editor.c',
    'silktex-editor.h',
    'silktex-compiler.c',
    'silktex-compiler.h',
    'silktex-preview.c',
    'silktex-preview.h',
    'silktex-main.c',
    'configfile.c',
    'configfile.h',
    'constants.h',
    'utils.c',
    'utils.h',
)

executable(
  'silktex',
  silktex_sources,
  silktex_resources,
  dependencies: project_dependencies,
  install : true,
  c_args : build_args,
)
