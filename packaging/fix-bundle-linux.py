#!/usr/bin/env python3

import os
import sys
import subprocess
import re
import shutil

sys.dont_write_bytecode=True
from fix_bundle import *
bundler=Bundler(sys.argv)
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
blacklist_f=args['libs_bl']

blacklist={}
with open(blacklist_f, 'r', encoding='utf-8') as i:
    for l in i:
        m=re.match(r'^\s*(.+)\s*$', l)
        if m:
            blacklist[m.group(1)]=True

qt_plugin_dir=os.path.join(prefix, lib_dir, 'qt')
os.makedirs(qt_plugin_dir, exist_ok=True)
qt_conf_path=os.path.join(prefix, bin_dir, 'qt.conf')
with open(qt_conf_path, 'w', encoding='utf-8') as o:
    o.write('[Paths]\n')
    o.write('Plugins='+os.path.join('..', lib_dir, 'qt')+'\n')

def schedule_add(to_add, a, b):
    k=os.path.join(lib_dir, a)
    if k in to_add:
        print('prev: ', to_add[k])
        print('cur : ', (a, b))
        #assert(to_add[k]==(a, b))
    else:
        to_add[k]=(a, b)

def fix_binary(k, f, fix_id, to_add, fixed_files):
    assert(k not in fixed_files)
    fixed_files[k]=True
    print('ldd ', f)
    # XXX -l???
    res=subprocess.run(['bash', '/usr/bin/ldd', os.path.join(prefix, f)], capture_output=True, text=True)
    if res.returncode!=0:
        print(res.stderr)
        exit(-1)
    for l in res.stdout.split('\n'):
        r=re.match(r'^\s*(.*) => (.+) \(0x[0-9a-f]+\)$', l)
        if r and r.group(1) not in blacklist:
            schedule_add(to_add, r.group(1), r.group(2))
        r=re.match(r'^\s*(.+/([^/]+)) \(0x[0-9a-f]+\)$', l)
        if r and r.group(2) not in blacklist:
            schedule_add(to_add, r.group(2), r.group(1))

def is_binary(f, deref):
    cmd=['file', '--mime']
    if not deref:
        cmd.append('-h')
    cmd.append(f)
    res=subprocess.run(cmd, capture_output=True, text=True)
    if res.returncode!=0:
        print(res.stderr)
        exit(-1)
    return re.match(r'.*: application/x(|-pie)-(sharedlib|executable);', res.stdout)

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
        'imageformats': ['libqsvg.so'],
        'iconengines': ['libqsvgicon.so'],
        'platforms': ['libqxcb.so'],
        'platformthemes': ['libqgtk3.so', 'libqxdgdesktopportal.so'],
        'xcbglintegrations': ['libqxcb-egl-integration.so', 'libqxcb-glx-integration.so'],
        }

for d,mods in qt_plugin_infos.items():
    os.makedirs(os.path.join(prefix, lib_dir, 'qt', d), exist_ok=True)
    for f in mods:
        fff=os.path.join(qt_dir, d, f)
        if not os.path.exists(fff):
            continue
        copy_file(fff, os.path.join(prefix, lib_dir, 'qt', d, f))
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
gtk_module_infos.clear()

for mod_dir2,info in gtk_module_infos.items():
    mod_dir=info[0]
    os.makedirs(os.path.join(prefix, mod_dir2), exist_ok=True)
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
        copy_file(v[1], os.path.join(prefix, k))
        fix_binary(k, k, True, to_add, fixed_files)

