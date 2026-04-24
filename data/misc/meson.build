
configure_file(input: 'gummi.desktop.in', output: 'gummi.desktop', install_dir: appdir, install: true, configuration: conf_data)
install_data(['default.tex',], install_dir: join_paths(gummidir,'misc/'))
install_man('gummi.1')

# Flatpak / XDG metadata named after the app-id so that Flatpak picks
# them up automatically at install time.
configure_file(
  input: 'app.silktex.SilkTex.desktop.in',
  output: 'app.silktex.SilkTex.desktop',
  install_dir: appdir,
  install: true,
  configuration: conf_data,
)

configure_file(
  input: 'app.silktex.SilkTex.metainfo.xml.in',
  output: 'app.silktex.SilkTex.metainfo.xml',
  install_dir: join_paths(datadir, 'metainfo'),
  install: true,
  configuration: conf_data,
)

