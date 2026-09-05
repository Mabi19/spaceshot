service_data = configuration_data()
service_data.set('bindir', get_option('prefix') / get_option('bindir'))
configure_file(
    input: 'land.mabi.spaceshot.service.in',
    output: 'land.mabi.spaceshot.service',
    configuration: service_data,
    install_dir: get_option('datadir') / 'dbus-1' / 'services',
)

dependencies = [
    dependency('gio-2.0', version: '>= 2.68'),
    dependency('glib-2.0'),
    dependency('gobject-2.0'),
    config_dep,
]

sources = files(
    'main.c',
    'server.c',
)

executable(
    'spaceshot-notify',
    sources,
    include_directories: build_conf_include,
    dependencies: dependencies,
    install: true,
)

install_man('spaceshot-notify.1')
