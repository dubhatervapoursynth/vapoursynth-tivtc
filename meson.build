project('TIVTC', 'cpp',
    default_options: ['buildtype=release', 'warning_level=2', 'b_lto=true', 'b_ndebug=if-release', 'cpp_std=c++20'],
    license: 'GPL-2.0-or-later',
    license_files: 'LICENSE',
    meson_version: '>=1.2.3',
    version: '4',
)

cflags = [
    '-Wshadow',
    '-Wno-unused-function',
]

ldflags = [
]

host_cpu_family = host_machine.cpu_family()

host_system = host_machine.system()

if host_system == 'windows' or host_system == 'cygwin'
    # mingw-w64 declares the fortified string functions but does not always ship them,
    # which turns into undefined references to __strcpy_chk and friends at link time.
    cflags += ['-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=0']

    # windows.h's min/max macros clash with std::min/std::max.
    cflags += ['-DNOMINMAX']
endif

sources = files(
    'src/calcCRC.cpp',
    'src/Cycle.cpp',
    'src/PluginInit.cpp',
    'src/TCommonASM.cpp',
    'src/TDecimate.cpp',
    'src/TDecimateASM.cpp',
    'src/TDecimateBlur.cpp',
    'src/TDecimateMode2.cpp',
    'src/TDecimateMode7.cpp',
    'src/TDecimateOut.cpp',
    'src/TFM.cpp',
    'src/TFMD2V.cpp',
    'src/TFMPlanar.cpp',
    'src/TFMPP.cpp',
)

py = import('python').find_installation(pure: false)

# Ask the VapourSynth Python module where its headers live rather than going through
# pkg-config, which is often absent on Windows and unnecessary here: the module is installed
# alongside every VapourSynth install and knows the answer already. Use the same interpreter
# that provides the install directory below, so the headers and the plugin directory always
# come from one VapourSynth install.
incdir = include_directories(
    run_command(
        py,
        '-c',
        'import vapoursynth as vs; print(vs.get_include())',
        check: true,
    ).stdout().strip(),
)

shared_module('tivtc',
    sources,
    gnu_symbol_visibility: 'hidden',
    include_directories: incdir,
    cpp_args: cflags,
    link_args: ldflags,
    install: true,
    install_dir: py.get_install_dir() / 'vapoursynth/plugins',
    name_prefix: '',
)
