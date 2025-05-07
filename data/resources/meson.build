# Compile resources
silktex_gresource = gnome.compile_resources('silktex',
  'letsgo.gresource.xml',
  gresource_bundle: true,
  install: true,
  install_dir: pkgdatadir,
)

# Also install the UI and CSS files directly for development/debugging
install_data('main.ui', install_dir: pkgdatadir + '/resources')
install_data('style.css', install_dir: pkgdatadir + '/resources')

# Create a symbolic link to make it easier to find during development
meson.add_install_script('sh', '-c', 
  'mkdir -p ${DESTDIR}@0@/silktex && ln -sf ${DESTDIR}@0@/silktex.gresource ${DESTDIR}@0@/silktex/'.format(pkgdatadir)
)
