import os
from waflib import Context

out = 'build'
top = '.'

VERSION = '2017.07'
APPNAME = 'glmark2'

FLAVORS = {
    'x11-gl' : 'glmark2',
    'x11-glesv2' : 'glmark2-es2',
    'drm-gl' : 'glmark2-drm',
    'drm-glesv2' : 'glmark2-es2-drm',
    'mir-gl' : 'glmark2-mir',
    'mir-glesv2' : 'glmark2-es2-mir',
    'wayland-gl' : 'glmark2-wayland',
    'wayland-glesv2' : 'glmark2-es2-wayland',
    'dispmanx-glesv2' : 'glmark2-es2-dispmanx',
    'malifb-glesv2' : 'glmark2-malifb',
    'hybrishwcomposer-glesv2' : 'glmark2-hybrishwcomposer',
}
FLAVORS_STR = ", ".join(FLAVORS.keys())

def option_list_cb(option, opt, value, parser):
    value = value.split(',')
    setattr(parser.values, option.dest, value)

def list_contains(lst, token):
    for e in lst:
        if token.endswith('$'):
            if e.endswith(token[:-1]): return True
        elif token in e: return True

    return False

def options(opt):
    opt.load('gnu_dirs')
    opt.load('compiler_c')
    opt.load('compiler_cxx')

    opt.add_option('--with-flavors', type = 'string', action='callback',
                   callback=option_list_cb,
                   dest = 'flavors',
                   help = "a list of flavors to build (%s, all (except dispmanx-glesv2))" % FLAVORS_STR)
    opt.parser.set_default('flavors', [])

    opt.add_option('--no-debug', action='store_false', dest = 'debug',
                   default = True, help='disable compiler debug information')
    opt.add_option('--no-opt', action='store_false', dest = 'opt',
                   default = True, help='disable compiler optimizations')
    opt.add_option('--data-path', action='store', dest = 'data_path',
                   help='path to main data (also see --data(root)dir)')
    opt.add_option('--extras-path', action='store', dest = 'extras_path',
                   help='path to additional data (models, shaders, textures)')

def configure(ctx):
    # Special 'all' flavor
    if 'all' in ctx.options.flavors:
        ctx.options.flavors = list(set(ctx.options.flavors) | set(FLAVORS.keys()))
        ctx.options.flavors.remove('all')
        # dispmanx is a special case, we don't want to include it in all
        ctx.options.flavors.remove('dispmanx-glesv2')

    # Ensure the flavors are valid
    for flavor in ctx.options.flavors:
       if flavor not in FLAVORS:
            ctx.fatal('Unknown flavor: %s. Supported flavors are %s' % (flavor, FLAVORS_STR))

    if not ctx.options.flavors:
        ctx.fatal('You need to select at least one flavor. Supported flavors are %s' % FLAVORS_STR)

    for flavor in FLAVORS:
        if flavor in ctx.options.flavors:
            ctx.env["FLAVOR_%s" % flavor.upper().replace('-','_')] = FLAVORS[flavor]

    ctx.load('gnu_dirs')
    ctx.load('compiler_c')
    ctx.load('compiler_cxx')

    # Check required headers
    req_headers = ['stdlib.h', 'string.h', 'unistd.h', 'stdint.h', 'stdio.h', 'jpeglib.h']
    for header in req_headers:
        ctx.check_cc(header_name = header, auto_add_header_name = True, mandatory = True)

    # Check for required libs
    req_libs = [('m', 'm'), ('jpeg', 'jpeg')]
    for (lib, uselib) in req_libs:
        ctx.check_cc(lib = lib, uselib_store = uselib)

    # Check required functions
    req_funcs = [('memset', 'string.h', []) ,('sqrt', 'math.h', ['m'])]
    for func, header, uselib in req_funcs:
        ctx.check_cc(function_name = func, header_name = header,
                      uselib = uselib, mandatory = True)

    # Check for a supported version of libpng
    supp_png_pkgs = (('libpng12', '1.2'), ('libpng15', '1.5'), ('libpng16', '1.6'),)
    have_png = False
    for (pkg, atleast) in supp_png_pkgs:
        try:
            pkg_ver = ctx.check_cfg(package=pkg, uselib_store='libpng', atleast_version=atleast,
                                    args = ['--cflags', '--libs'])
        except:
            continue
        else:
            have_png = True
            break

    if not have_png:
        ctx.fatal('You need to install a supported version of libpng: ' + str(supp_png_pkgs))

    dispmanx = 'dispmanx-glesv2' in ctx.options.flavors
    if dispmanx:
        # dispmanx uses custom Broadcom libraries that don't follow standard
        # Linux packaging.  Just force the library setup here.
        if len(ctx.options.flavors) != 1:
            ctx.fatal("dispmanx can't be built with any other flavor")

        ctx.env.append_value('CXXFLAGS', '-I/opt/vc/include')

        ctx.check_cxx(lib = 'brcmGLESv2', uselib_store = 'glesv2', libpath='/opt/vc/lib')
        ctx.check_cxx(lib = ['brcmEGL', 'brcmGLESv2'], uselib_store = 'egl', libpath='/opt/vc/lib')
        ctx.check_cxx(lib = ['bcm_host', 'vcos', 'vchiq_arm'], uselib_store = 'dispmanx', libpath='/opt/vc/lib')

    # Check optional packages
    opt_pkgs = [('x11', 'x11', None, list_contains(ctx.options.flavors, 'x11')),
                ('gl', 'gl', None, list_contains(ctx.options.flavors, 'gl$') and not dispmanx),
                ('egl', 'egl', None, list_contains(ctx.options.flavors, 'glesv2$') and not dispmanx),
                ('glesv2', 'glesv2', None, list_contains(ctx.options.flavors, 'glesv2$') and not dispmanx),
                ('libdrm','drm', None, list_contains(ctx.options.flavors, 'drm')),
                ('gbm','gbm', None, list_contains(ctx.options.flavors, 'drm')),
                ('libudev', 'udev', None, list_contains(ctx.options.flavors, 'drm')),
                ('mirclient','mirclient', '0.13', list_contains(ctx.options.flavors, 'mir')),
                ('wayland-client','wayland-client', None, list_contains(ctx.options.flavors, 'wayland')),
                ('wayland-egl','wayland-egl', None, list_contains(ctx.options.flavors, 'wayland')),
                ('hwcomposer-egl','hwcomposer-egl', None, list_contains(ctx.options.flavors, 'hybrishwcomposer')),
                ('hybris-egl-platform','hybris-egl-platform', None, list_contains(ctx.options.flavors, 'hybrishwcomposer')),
                ('libhardware','libhardware', None, list_contains(ctx.options.flavors, 'hybrishwcomposer')),
		]
    for (pkg, uselib, atleast, mandatory) in opt_pkgs:
        if atleast is None:
            ctx.check_cfg(package = pkg, uselib_store = uselib,
                          args = '--cflags --libs', mandatory = mandatory)
        else:
            ctx.check_cfg(package = pkg, uselib_store = uselib, atleast_version=atleast,
                          args = '--cflags --libs', mandatory = mandatory)

    if list_contains(ctx.options.flavors, 'hybrishwcomposer'):
        ctx.env.append_unique('CXXFLAGS', '-I/usr/local/include -I/usr/local/include/hybris -I/usr/local/include/hybris/hwcomposerwindow -I/usr/local/include/hybris/eglplatformcommon -I/usr/local/include/hybris/android-headers-22'.split(' '))
	ctx.env.append_unique('LINKFLAGS', '-L/usr/local/lib -lhardware -lhybris-hwcomposerwindow -lhybris-common -lhybris-eglplatformcommon -lEGL -lsync'.split(' '))

    # Prepend CXX flags so that they can be overriden by the
    # CXXFLAGS environment variable
    if ctx.options.opt:
        ctx.env.prepend_value('CXXFLAGS', '-O2')
    if ctx.options.debug:
        ctx.env.prepend_value('CXXFLAGS', '-g')
    ctx.env.prepend_value('CXXFLAGS', '-std=c++14 -Wall -Wextra -Wnon-virtual-dtor'.split(' '))

    ctx.env.HAVE_EXTRAS = False
    if ctx.options.extras_path is not None:
        ctx.env.HAVE_EXTRAS = True
        ctx.env.append_unique('GLMARK_EXTRAS_PATH', ctx.options.extras_path)
        ctx.env.append_unique('DEFINES', 'GLMARK_EXTRAS_PATH="%s"' % ctx.options.extras_path)

    if ctx.options.data_path is not None:
        data_path = ctx.options.data_path 
    else:
        data_path = os.path.join(ctx.env.DATADIR, 'glmark2')

    ctx.env.append_unique('GLMARK_DATA_PATH', data_path)
    ctx.env.append_unique('DEFINES', 'GLMARK_DATA_PATH="%s"' % data_path)
    ctx.env.append_unique('DEFINES', 'GLMARK_VERSION="%s"' % VERSION)
    ctx.env.GLMARK2_VERSION = VERSION

    ctx.msg("Prefix", ctx.env.PREFIX, color = 'PINK')
    ctx.msg("Data path", data_path, color = 'PINK')
    ctx.msg("Including extras", "Yes" if ctx.env.HAVE_EXTRAS else "No",
            color = 'PINK');
    if ctx.env.HAVE_EXTRAS:
        ctx.msg("Extras path", ctx.options.extras_path, color = 'PINK')
    ctx.msg("Building flavors", ctx.options.flavors)

def build(ctx):
    ctx.recurse('src')
    ctx.recurse('data')
    ctx.recurse('doc')

class Glmark2Dist(Context.Context):
    """ Custom dist command that preserves symbolic links"""

    cmd = "dist"

    def execute(self):
        self.recurse([os.path.dirname(Context.g_module.root_path)])
        self.archive()

    def get_files(self):
        import fnmatch
        files = []
        excludes = ['*.bzr', '*.git', '*~', './.*waf*', './build*', '*.swp', '*.pyc', '*glmark2-*.tar.gz']
        for (dirpath, dirnames, filenames) in os.walk(top):
            names_to_remove = []
            names = dirnames + filenames
            for n in names:
                for exclude in excludes:
                    if fnmatch.fnmatch(os.path.join(dirpath, n), exclude):
                        names_to_remove.append(n)
                        break

            for d in names_to_remove:
                if d in dirnames:
                    dirnames.remove(d)
                if d in filenames:
                    filenames.remove(d)

            files.extend([os.path.join(dirpath, d) for d in dirnames])
            files.extend([os.path.join(dirpath, f) for f in filenames])

        return files

    def archive(self):
        import tarfile
        tar = tarfile.open(APPNAME + '-' + VERSION + '.tar.gz', 'w:gz')
        for f in self.get_files():
            tar.add(f, arcname = APPNAME + '-' + VERSION + '/' + f, recursive = False)
        tar.close()
