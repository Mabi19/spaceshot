add_languages('vala', native: false)

service_data = configuration_data()
service_data.set('bindir', get_option('prefix') / get_option('bindir'))
configure_file(
    input: 'land.mabi.spaceshot.service.in',
    output: 'land.mabi.spaceshot.service',
    configuration: service_data,
    install_dir: get_option('datadir') / 'dbus-1' / 'services',
)

dependencies = [
    dependency('gio-2.0'),
    dependency('glib-2.0'),
    dependency('gobject-2.0'),
    meson.get_compiler('vala').find_library('posix'),
    config_dep_vala,
]

sources = files(
    'file-manager.vala',
    'main.vala',
    'notification.vala',
    'server.vala',
)

executable(
    'spaceshot-notify',
    sources,
    dependencies: dependencies,
    c_args: ['-w'], # disable C warnings
    install: true,
)
