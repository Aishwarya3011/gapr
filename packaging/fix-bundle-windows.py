#!/usr/bin/env python3

import os
import sys
import subprocess
import re
import shutil

sys.dont_write_bytecode=True
from fix_bundle import *
bundler=BundlerWindows(sys.argv)
prefix=bundler.pfx


args=bundler.args
dist_type=args['dist_type']
if dist_type=='system':
    exit(0)
###############################

bin_dir=args['bindir']
lib_dir=args['libdir']
qt_dir=args['qt_plugin_dir']
gdk_pixbuf_dir=args['gdk_pixbuf_dir']
gio_modules_dir=args['gio_module_dir']
gtk_immodules_dir=args['im_module_dir']


aux_binaries=['gdbus.exe']

qt_plugin_dir=os.path.join(prefix, lib_dir, 'qt')
os.makedirs(qt_plugin_dir, exist_ok=True)
qt_conf_path=os.path.join(prefix, bin_dir, 'qt.conf')
with open(qt_conf_path, 'w', encoding='utf-8') as o:
    o.write('[Paths]\n')
    o.write('Plugins='+os.path.join('..', lib_dir, 'qt')+'\n')

def schedule_add(to_add, a):
    k=os.path.join(bin_dir, os.path.basename(a))
    if k in to_add:
        print('prev: ', to_add[k])
        print('cur : ', a)
        #assert(to_add[k]==a)
    else:
        to_add[k]=a

os.environ['PATHEXT']='.EXE;.DLL'
def get_dll(name):
    if args.get('sys_root') is None:
        return shutil.which(name, mode=os.F_OK)
    return shutil.which(name, mode=os.F_OK, path=os.path.join(args.get('sys_root'), 'bin'))

objdump=args.get('objdump', 'objdump')
def fix_binary(k, f, fix_id, to_add, fixed_files):
    assert(k not in fixed_files)
    fixed_files[k]=True
    print(objdump, f)
    res=subprocess.run([objdump, '-p', os.path.join(prefix, f)], capture_output=True, text=True)
    if res.returncode!=0:
        print(res.stderr)
        exit(-1)
    for l in res.stdout.split('\n'):
        r=re.match(r'^\s*DLL Name: (.+\.dll)$', l, re.IGNORECASE)
        if not r:
            continue
        dll=get_dll(r.group(1))
        if dll is None:
            continue
        r=re.match(r'^\w:[\\/]Windows[\\/]System32[\\/].*', dll, re.IGNORECASE)
        if r:
            continue
        r=re.match(r'.*mingw.*', dll, re.IGNORECASE)
        if not r:
            print ('XXX: ', dll)
            continue
        schedule_add(to_add, dll)

def is_binary(f, deref):
    return re.match(r'.*(\.dll|\.exe)$', f, re.IGNORECASE)

def fix_directory(d, fix_id, to_add, fixed_files):
    print('fix directory: ', os.path.join(prefix, d))
    for f in os.listdir(os.path.join(prefix, d)):
        f_path=os.path.join(prefix, d, f)
        print('try '+f_path)
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

for b in aux_binaries:
    dst=os.path.join(prefix, 'bin', b)
    src=get_dll(b)
    copy_file(src, dst)
    k=os.path.join('bin', b)
    fix_binary(k, k, False, to_add, fixed_files)

qt_plugin_infos={
        'imageformats': ['qsvg.dll', 'qico.dll'],
        'iconengines': ['qsvgicon.dll'],
        'platforms': ['qwindows.dll'],
        'styles': ['qwindowsvistastyle.dll'],
        }

for d,mods in qt_plugin_infos.items():
    os.makedirs(os.path.join(prefix, lib_dir, 'qt', d), exist_ok=True)
    for f in mods:
        copy_file(os.path.join(qt_dir, d, f), os.path.join(prefix, lib_dir, 'qt', d, f))
    fix_directory(os.path.join(lib_dir, 'qt', d), True, to_add, fixed_files)

###XXX
def gen_cache_and_fix(mod_dir2, cachef, cmd, env_add, rel_path):
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
        pat=os.path.join(prefix, '')
        pat2=None
        if args.get('sys_root') is not None:
            pat2=os.path.join(args.get('sys_root'), '')
        for l in res.stdout.split('\n'):
            #bin/bin
            l=l.replace(pat, rel_path)
            if pat2 is not None:
                l=l.replace(pat2, rel_path)
            o.write(l+'\n')


def gen_cache_gdk_pixbuf(mod_dir2):
    gen_cache_and_fix(mod_dir2, os.path.join(prefix, mod_dir2)+'.cache', bundler.get_cmd('gdk-pixbuf-query-loaders'), {'GDK_PIXBUF_MODULEDIR': os.path.join(prefix, mod_dir2)}, os.path.join('.', ''))

def gen_cache_gtk_immodules(mod_dir2):
    gen_cache_and_fix(mod_dir2, os.path.join(prefix, mod_dir2)+'.cache', bundler.get_cmd('gtk-query-immodules-3.0'), {'GTK_EXE_PREFIX': os.path.join(prefix)}, os.path.join('..', ''))

def gen_cache_gio(mod_dir2):
    cmd=bundler.get_cmd('gio-querymodules')
    cmd+=[os.path.join(prefix, mod_dir2)]
    print('running ', cmd)
    res=subprocess.run(cmd)
    if res.returncode!=0:
        print(res.stderr)
        exit(-1)

gtk_module_infos={
        'lib/gdk-pixbuf-2.0/2.10.0/loaders': (gdk_pixbuf_dir, gen_cache_gdk_pixbuf),
        'lib/gio/modules': (gio_modules_dir, gen_cache_gio),
        'lib/gtk-3.0/3.0.0/immodules': (gtk_immodules_dir, gen_cache_gtk_immodules),
        }

###########################################
if dist_type!='full':
    gtk_module_infos.clear()

for mod_dir2,info in gtk_module_infos.items():
    mod_dir=info[0]
    os.makedirs(os.path.join(prefix, mod_dir2), exist_ok=True)
    for f in os.listdir(mod_dir) if os.path.isdir(mod_dir) else []:
        ff=os.path.join(mod_dir, f)
        if is_binary(ff, True):
            copy_file(ff, os.path.join(prefix, mod_dir2, f))
    func=info[1]
    if func is not None:
        func(mod_dir2)
    fix_directory(mod_dir2, False, to_add, fixed_files)

###
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
        copy_file(v, os.path.join(prefix, k))
        fix_binary(k, k, True, to_add, fixed_files)

bundler.run()

##########

# env vars
#XDG_DATA_DIRS
