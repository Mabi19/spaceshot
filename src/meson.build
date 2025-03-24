wl_dep = dependency('wayland-client')

# get extra protocols
extra_protocols = [
    {
        'provider': 'wlr',
        'state': 'unstable',
        'name': 'wlr-layer-shell',
        'version': 1,
    },
    {
        'provider': 'wlr',
        'state': 'unstable',
        'name': 'wlr-screencopy',
        'version': 1,
    },
    {
        'provider': 'wp',
        'state': 'staging',
        'name': 'cursor-shape',
        'version': 1,
    },
    {
        'provider': 'wp',
        'state': 'staging',
        'name': 'fractional-scale',
        'version': 1,
    },
    {
        # required for cursor-shape
        'provider': 'wp',
        'state': 'stable',
        'name': 'tablet',
        'version': 2,
    },
    {
        'provider': 'wp',
        'state': 'stable',
        'name': 'viewporter',
    },
    {
        # required for wlr-layer-shell
        'provider': 'wp',
        'state': 'stable',
        'name': 'xdg-shell',
    },
    {
        'provider': 'wp',
        'state': 'unstable',
        'name': 'xdg-output',
        'version': 1,
    },
]

wl_scanner = find_program('wayland-scanner')
wlr_protocols_dep = dependency('wlr-protocols')
wlr_protocols_dir = wlr_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')
wayland_protocols_dep = dependency('wayland-protocols')
wayland_protocols_dir = wayland_protocols_dep.get_variable(pkgconfig: 'pkgdatadir')

extra_protocol_deps = []
foreach protocol : extra_protocols
    provider = protocol['provider']
    state = protocol['state']
    name = protocol['name']

    # protocol paths depend on state
    if state != 'unstable'
        if 'version' in protocol
            version = protocol['version']
            filename = f'@name@-v@version@.xml'
        else
            filename = f'@name@.xml'
        endif
    else
        version = protocol['version']
        filename = f'@name@-@state@-v@version@.xml'
    endif

    if provider == 'wlr'
        xml_path = wlr_protocols_dir / state / filename
    elif provider == 'wp'
        xml_path = wayland_protocols_dir / state / name / filename
    endif

    xml_file = files(xml_path)[0]
    source_c = custom_target(
        f'@name@-client.c',
        output: f'@name@-client.c',
        input: xml_file,
        command: [wl_scanner, '-s', 'private-code', '@INPUT@', '@OUTPUT@'],
    )
    source_h = custom_target(
        f'@name@-client.h',
        output: f'@name@-client.h',
        input: xml_file,
        command: [wl_scanner, '-s', 'client-header', '@INPUT@', '@OUTPUT@'],
    )
    protocol_dep = declare_dependency(sources: [source_c, source_h])
    extra_protocol_deps += protocol_dep
endforeach

sources = [
    'args.c',
    'bbox.c',
    'image.c',
    'link-buffer.c',
    'log.c',
    'main.c',
    'paths.c',
    'region-picker.c',
    'wayland/globals.c',
    'wayland/overlay-surface.c',
    'wayland/render.c',
    'wayland/screenshot.c',
    'wayland/seat.c',
    'wayland/shared-memory.c',
]

cairo_dep = dependency('cairo')
spng_dep = dependency('spng')
# TODO: add an option to compile a custom libpng, probably with zlib-ng
libpng_dep = dependency('libpng')
xkbcommon_dep = dependency('xkbcommon')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

executable(
    'spaceshot',
    sources,
    include_directories: build_conf_include,
    dependencies: [
        cairo_dep,
        libpng_dep,
        spng_dep,
        xkbcommon_dep,
        m_dep,
        wl_dep,
        extra_protocol_deps,
    ],
)
