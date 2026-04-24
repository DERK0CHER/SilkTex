
install_data(['gummi.png',], install_dir: join_paths(icondir,'512x512/apps/'))
install_data(['icon.png',], install_dir: join_paths(gummidir,'icons/'))

# Also install the app icon under the Flatpak app-id so the desktop
# entry / metainfo can reference it without relying on legacy names.
install_data(
  'icon.png',
  install_dir: join_paths(icondir, '256x256/apps/'),
  rename: 'app.silktex.SilkTex.png',
)

install_data(
  'gummi.png',
  install_dir: join_paths(icondir, '512x512/apps/'),
  rename: 'app.silktex.SilkTex.png',
)

