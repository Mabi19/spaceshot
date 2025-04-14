add_languages('vala', native: false)

dependencies = [
    dependency('gio-2.0'),
    dependency('glib-2.0'),
    dependency('gobject-2.0'),
]

sources = files([
    'main.vala',
    'notification.vala',
    'server.vala',
])

executable(
    'spaceshot-notify',
    sources,
    dependencies: dependencies,
    c_args: ['-w'], # disable C warnings
    install: true
)
