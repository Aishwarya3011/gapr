#!/usr/bin/env python3

import os
import sys
import subprocess
import re
import shutil

sys.dont_write_bytecode=True
from fix_bundle import *
bundler=BundlerMac(sys.argv)
prefix=bundler.pfx


args=bundler.args
inst_type=args['dist_type']
if inst_type=='system':
    exit(0)

bin_dir=args['bindir']
lib_dir=args['libdir']
qt_dir=args['qt_plugin_dir']
gdk_pixbuf_dir=args['gdk_pixbuf_dir']
gio_modules_dir=args['gio_module_dir']
gtk_immodules_dir=args['im_module_dir']

qt_plugin_dir=os.path.join(prefix, lib_dir, 'qt')
os.makedirs(qt_plugin_dir, exist_ok=True)
qt_conf_path=os.path.join(prefix, bin_dir, 'qt.conf')
with open(qt_conf_path, 'w', encoding='utf-8') as o:
    o.write('[Paths]\n')
    o.write('Plugins='+os.path.join('..', lib_dir, 'qt')+'\n')

def schedule_add(to_add, a, b, cmd)->int:
    k=os.path.join(lib_dir, os.path.basename(a))
    c=''
    if b is not None:
        c=b
    cmd.append('-change')
    cmd.append(a+c)
    cmd.append(os.path.join('@executable_path', '..', k+c))
    if bundler.relocate_intl(cmd):
        return 1
    if k in to_add:
        print('prev: ', to_add[k])
        print('cur : ', (a, b))
        #assert(to_add[k]==(a, b))
    else:
        to_add[k]=(a, b)
    return 0

def fix_binary(k, f, fix_id, to_add, fixed_files):
    assert(k not in fixed_files)
    fixed_files[k]=True
    cmd=['install_name_tool']
    if fix_id:
        cmd.append('-id')
        cmd.append(os.path.join('@executable_path', '..', f))
    print('otool -L -X ', f)
    # XXX -l???
    res=subprocess.run(['otool', '-L', '-X', os.path.join(prefix, f)], capture_output=True, text=True)
    if res.returncode!=0:
        print(res.stderr)
        exit(-1)
    fix_intl=0
    for l in res.stdout.split('\n'):
        r=re.match(r'^\s*(/usr/lib/|/System/).*\s.*', l)
        if r:
            continue
        r=re.match(r'^\s*([/A-Za-z\.].+\.dylib)\s.*', l)
        if r:
            fix_intl+=schedule_add(to_add, r.group(1), None, cmd)
            continue
        r=re.match(r'^\s*([/A-Za-z\.].+\.framework)([^A-Za-z0-9][A-Za-z0-9/]*)\s.*', l)
        if r:
            fix_intl+=schedule_add(to_add, r.group(1), r.group(2), cmd)
    cmd.append(os.path.join(prefix, f))
    # XXX -delete_rpath???
    print('fix: ', cmd)
    res=subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode!=0:
        print(res.stderr)
        exit(-1)
    if fix_intl>0:
        bundler.replace_symbol(os.path.join(prefix, f), [
            (b'_libintl_bindtextdomain', b'_X_reloc_bindtextdomain'),
            ])

def is_binary(f, deref):
    cmd=['file', '--mime']
    if not deref:
        cmd.append('-h')
    cmd.append(f)
    res=subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode!=0:
        print(res.stderr)
        exit(-1)
    return re.match(r'.*: application/x-mach-binary;', res.stdout)

def fix_directory(d, fix_id, to_add, fixed_files):
    for f in os.listdir(os.path.join(prefix, d)):
        f_path=os.path.join(prefix, d, f)
        #print('try '+f_path+'\n'+res.stdout)
        if is_binary(f_path, False):
            ff=os.path.join(d, f)
            print('fix directory entry: ', ff)
            fix_binary(ff, ff, fix_id, to_add, fixed_files)

to_add={}
fixed_files={}
print('fix installed\n')
fix_directory(bin_dir, False, to_add, fixed_files)
fix_directory(lib_dir, True, to_add, fixed_files)

def copy_file(src, dst):
    shutil.copyfile(src, dst)
    #chmod -R u+w

qt_plugin_infos={
        'imageformats': ['libqsvg.dylib', 'libqicns.dylib'],
        'iconengines': ['libqsvgicon.dylib'],
        'platforms': ['libqcocoa.dylib'],
        'styles': ['libqmacstyle.dylib'],
        }
for d,mods in qt_plugin_infos.items():
    os.makedirs(os.path.join(prefix, lib_dir, 'qt', d), exist_ok=True)
    for f in mods:
        copy_file(os.path.join(qt_dir, d, f), os.path.join(prefix, lib_dir, 'qt', d, f))
    fix_directory(os.path.join(lib_dir, 'qt', d), True, to_add, fixed_files)

def gen_cache_and_fix(mod_dir2, cachef, cmd, env_add):
    env2=os.environ.copy()
    for k, v in env_add.items():
        env2[k]=v
    print('running ', cmd)
    res=subprocess.run(cmd, env=env2, capture_output=True, text=True)
    if res.returncode!=0:
        print(res.stderr)
        exit(-1)
    print(res.stderr)
    with open(cachef, 'w', encoding='utf-8') as o:
        ppp='"'+os.path.join(prefix, '')
        for l in res.stdout.split('\n'):
            if l.startswith(ppp):
                l='"'+os.path.join('@executable_path', '..', l[len(ppp):])
            o.write(l+'\n')

def gen_cache_gdk_pixbuf(mod_dir2):
    gen_cache_and_fix(mod_dir2, os.path.join(prefix, mod_dir2)+'.cache', ['gdk-pixbuf-query-loaders'], {'GDK_PIXBUF_MODULEDIR': os.path.join(prefix, mod_dir2)})

def gen_cache_gtk_immodules(mod_dir2):
    gen_cache_and_fix(mod_dir2, os.path.join(prefix, mod_dir2)+'.cache', ['gtk-query-immodules-3.0'], {'GTK_EXE_PREFIX': os.path.join(prefix)})

def gen_cache_gio(mod_dir2):
    cmd=['gio-querymodules', os.path.join(prefix, mod_dir2)]
    print('running ', cmd)
    res=subprocess.run(cmd)
    if res.returncode!=0:
        print(res.stderr)
        exit(-1)

gtk_module_infos={
        'lib/gdk-pixbuf-loaders': (gdk_pixbuf_dir, gen_cache_gdk_pixbuf),
        'lib/gio-modules': (gio_modules_dir, gen_cache_gio),
        'lib/gtk-3.0/immodules': (gtk_immodules_dir, gen_cache_gtk_immodules),
        }
if inst_type!='full':
    gtk_module_infos.clear()

for mod_dir2,info in gtk_module_infos.items():
    mod_dir=info[0]
    os.makedirs(os.path.join(prefix, mod_dir2), exist_ok=True)
    if not os.path.isdir(mod_dir):
        continue
    for f in os.listdir(mod_dir):
        ff=os.path.join(mod_dir, f)
        if is_binary(ff, True):
            copy_file(ff, os.path.join(prefix, mod_dir2, f))
    func=info[1]
    if func is not None:
        func(mod_dir2)
    fix_directory(mod_dir2, False, to_add, fixed_files)

I=0
while True:
    I=I+1
    to_fix={}
    for k, v in to_add.items():
        if k not in fixed_files:
            to_fix[k]=v
    if len(to_fix)==0:
        break
    to_add={}
    for k, v in to_fix.items():
        print('fix recurse ', I, ': ', k, v)
        if v[1] is None:
            copy_file(v[0], os.path.join(prefix, k))
            fix_binary(k, k, True, to_add, fixed_files)
        else:
            os.makedirs(os.path.dirname(os.path.join(prefix, k+v[1])), exist_ok=True)
            # XXX copy dir???
            copy_file(v[0]+v[1], os.path.join(prefix, k+v[1]))
            fix_binary(k, k+v[1], True, to_add, fixed_files)

bundler.run()

