project('TIVTC', 'cpp',
        version: '4',
        default_options: ['cpp_std=c++20', 'buildtype=release'],
        meson_version: '>=1.2.3')


warnings = [
  '-Wall',
  '-Wextra',
  '-Wshadow',
  '-Wno-unused-function',
]

cflags = [
  warnings,
  '-fvisibility=hidden',
]

ldflags = [
]


host_cpu_family = host_machine.cpu_family()


host_system = host_machine.system()

if host_system == 'windows' or host_system == 'cygwin'
  if host_cpu_family == 'x86'
    cflags += '-mstackrealign'
    ldflags += '-Wl,--kill-at'
  endif

  # Avoid linking errors:
  # /usr/lib/gcc/x86_64-w64-mingw32/10.2.0/../../../../x86_64-w64-mingw32/bin/ld: libtivtc.dll.p/src_TFM.cpp.obj:TFM.cpp:(.text+0x9019): undefined reference to `__strcat_chk' 
  cflags += ['-U_FORTIFY_SOURCE', '-D_FORTIFY_SOURCE=0']

  # windows.h's min/max macros clash with std::min/std::max.
  cflags += '-DNOMINMAX'
endif


cxx = meson.get_compiler('cpp')

cxx_id = cxx.get_id()

if cxx_id.startswith('clang')
  cflags += '-DCLANG=1'
endif

if cxx_id == 'gcc'
  cflags += '-DGCC=1'
endif


sources = [
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
]

py = import('python').find_installation(pure: false)

# Ask the VapourSynth Python module where its headers live rather than going through
# pkg-config, which is often absent on Windows and unnecessary here: the module is installed
# alongside every VapourSynth install and knows the answer already.
incdir = include_directories(
  run_command(py, '-c', 'import vapoursynth as vs; print(vs.get_include())', check: true).stdout().strip(),
)

shared_module('tivtc',
              sources,
              include_directories: incdir,
              link_args: ldflags,
              cpp_args: cflags,
              install: true)
