wl_dep = dependency('wayland-client')

# get extra protocols
extra_protocols = [
    {
        'provider': 'vendor',
        'state': 'unstable',
        'name': 'wlr-layer-shell',
        'version': 1,
    },
    {
        'provider': 'vendor',
        'state': 'unstable',
        'name': 'wlr-screencopy',
        'version': 1,
    },
    {
        'provider': 'wp',
        'state': 'staging',
        'name': 'ext-data-control',
        'version': 1,
    },
    {
        'provider': 'wp',
        'state': 'staging',
        'name': 'ext-image-capture-source',
        'version': 1,
    },
    {
        'provider': 'wp',
        'state': 'staging',
        'name': 'ext-image-copy-capture',
        'version': 1,
    },
    {
        'provider': 'wp',
        'state': 'staging',
        'name': 'ext-foreign-toplevel-list',
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
vendor_protocols_dir = '../protocol'
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

    if provider == 'vendor'
        xml_path = vendor_protocols_dir / filename
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

cairo_dep = dependency('cairo')
egl_dep = dependency('egl')
glesv2_dep = dependency('glesv2')
wayland_egl_dep = dependency('wayland-egl')
# TODO: add an option to compile a custom libpng, probably with zlib-ng
libpng_dep = dependency('libpng')
libxxhash_dep = dependency('libxxhash')
pango_dep = dependency('pango')
pangocairo_dep = dependency('pangocairo')
xkbcommon_dep = dependency('xkbcommon')

cc = meson.get_compiler('c')
m_dep = cc.find_library('m', required: false)

sources = files(
    'render/cairo.c',
    'render/common.c',
    'render/gl.c',
    'render/renderer.c',
    'wayland/clipboard-common.c',
    'wayland/clipboard-core.c',
    'wayland/clipboard-ext.c',
    'wayland/globals.c',
    'wayland/overlay-surface.c',
    'wayland/screen-capture-common.c',
    'wayland/screen-capture-ext.c',
    'wayland/screen-capture-wlr.c',
    'wayland/seat.c',
    'wayland/shared-memory.c',
    'args.c',
    'bbox.c',
    'debug.c',
    'image.c',
    'link-buffer.c',
    'log.c',
    'main.c',
    'output-picker.c',
    'paths.c',
    'region-picker.c',
    'smart-border.c',
)

executable(
    'spaceshot',
    sources,
    include_directories: build_conf_include,
    dependencies: [
        cairo_dep,
        egl_dep,
        glesv2_dep,
        wayland_egl_dep,
        libpng_dep,
        libxxhash_dep,
        pango_dep,
        pangocairo_dep,
        xkbcommon_dep,
        m_dep,
        wl_dep,
        extra_protocol_deps,
        config_dep,
    ],
    install: true,
)
