
blueprint_compiler = find_program('blueprint-compiler')

ui_blueprints = [
  'main.blp',
  'prefs.blp',
  'snippets.blp',
]

foreach blp : ui_blueprints
  compiled_ui = custom_target(
    blp + '-compiled',
    input: blp,
    output: blp.replace('.blp', '.ui'),
    command: [blueprint_compiler, 'compile', '--output', '@OUTPUT@', '@INPUT@'],
    build_by_default: true,
    install: true,
    install_dir: join_paths(gummidir, 'ui'),
  )
endforeach


