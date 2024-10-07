#!/usr/bin/env python3

import os
import re
import shutil
import subprocess
import sys

USED_ICONS=set([
    'application-exit-symbolic',
    'application-x-executable-symbolic',
    'changes-allow-symbolic',
    'changes-prevent-symbolic',
    'configure-symbolic',
    'dialog-cancel-symbolic',
    'dialog-ok-symbolic',
    'dialog-password-symbolic',
    'document-import-symbolic',
    'document-new-symbolic',
    'document-open-recent-symbolic',
    'document-open-remote-symbolic',
    'document-open-symbolic',
    'document-properties-symbolic',
    'document-save-as-symbolic',
    'document-save-symbolic',
    'edit-cut-symbolic',
    'edit-find-symbolic',
    'edit-redo-symbolic',
    'edit-rename-symbolic',
    'edit-undo-symbolic',
    'emblem-important-symbolic',
    'find-location-symbolic',
    'go-first-symbolic',
    'go-jump-symbolic',
    'go-next-symbolic',
    'help-about-symbolic',
    'help-contents-symbolic',
    'image-missing',
    'list-add-symbolic',
    'list-remove-symbolic',
    'open-menu-symbolic',
    'plugins-symbolic',
    'process-stop-symbolic',
    'process-working-symbolic',
    'system-run-symbolic',
    'view-refresh-symbolic',
    'window-close-symbolic',
    'window-maximize-symbolic',
    'window-minimize-symbolic',
    'window-restore-symbolic',
    'zoom-fit-best-symbolic',
    'zoom-original-symbolic',
    ])

class Bundler:
    def __init__(self, argv):
        if os.environ.get('DESTDIR') is None and os.environ.get('FLATPAK_DEST') is None:
            exit(0)
        prefix=os.environ.get('MESON_INSTALL_DESTDIR_PREFIX')
        if prefix is None:
            exit(-1)
        self.verbose=os.environ.get('MESON_INSTALL_QUIET') is None
        self.pfx=prefix
        self.args={}
        for i in range(1, len(argv)):
            r=re.match(r'^([a-z_0-9]+)=(.*)$', argv[i])
            if not r:
                exit(-1)
            self.args[r.group(1)]=r.group(2)

    @staticmethod
    def _icon_ignore(path, items):
        res=[]
        for i in items:
            if i in set(['icon-theme.cache', 'legacy']):
                res.append(i)
                continue
            r=re.match(r'^([^\.]+).*\.(svg|png|cur|ani)$', i, re.IGNORECASE)
            if not r:
                continue
            if re.match(r'.*\bcursors\b.*', path, re.IGNORECASE):
                continue
            if r.group(1) in USED_ICONS:
                continue
            res.append(i)
        return res

    @staticmethod
    def copy_icon_theme(src, dst, cmd):
        print('copy_icon_theme: ', src, ' -> ', dst)
        os.makedirs(dst, exist_ok=True)
        if os.path.isdir(src):
            shutil.copytree(src, dst, symlinks=False, ignore=Bundler._icon_ignore, dirs_exist_ok=True)
        if cmd is None:
            return
        cmd+=['--index-only', dst]
        print('running ', cmd)
        res=subprocess.run(cmd, text=True)
        if res.returncode!=0:
            exit(-1)

    @staticmethod
    def copy_schema_files(src, dst, cmd, schemas):
        print('copy_schema_files: ', src, ' -> ', dst)
        os.makedirs(dst, exist_ok=True)
        for s in schemas:
            fn=s+'.gschema.xml'
            srcfile=os.path.join(src, fn)
            if os.path.isfile(srcfile):
                shutil.copyfile(srcfile, os.path.join(dst, fn))
        if cmd is None:
            return
        cmd+=[dst]
        print('running ', cmd)
        res=subprocess.run(cmd, text=True)
        if res.returncode!=0:
            exit(-1)

    @staticmethod
    def copy_locale(src, dst, mo_files):
        print('copy_locale: ', src, ' -> ', dst)
        dst=os.path.join(dst, 'LC_MESSAGES')
        src=os.path.join(src, 'LC_MESSAGES')
        os.makedirs(dst, exist_ok=True)
        for mo in mo_files:
            fn=mo+'.mo'
            srcfile=os.path.join(src, fn)
            if os.path.isfile(srcfile):
                shutil.copyfile(srcfile, os.path.join(dst, fn))

    @staticmethod
    def replace_symbol(fn, chgs):
        with open(fn, 'r+b') as f:
            c=f.read()
            for a, b in chgs:
                if len(a)!=len(b):
                    exit(-1)
                c=c.replace(a, b)
            f.seek(0)
            f.write(c)

class BundlerMac(Bundler):
    def __init__(self, argv):
        super().__init__(argv)

    @staticmethod
    def get_cmd(name):
        return [name]

    def sys_root(self):
        return self.args.get('sys_root', '/usr/local')

    @staticmethod
    def relocate_intl(cmd):
        r=re.match(r'.*/(libintl.\d+.dylib)$', cmd[-1])
        if not r:
            return False
        if re.match(r'.*/libintl-reloc.dylib', cmd[2]):
            return False
        cmd[-1]=cmd[-1][0:r.span(1)[0]]+'libintl-reloc.dylib'
        return True

    def copy_icon_themes(self):
        for t in ['hicolor', 'Adwaita']:
            self.copy_icon_theme(os.path.join(self.sys_root(), 'share', 'icons', t),
                    os.path.join(self.pfx, 'share', 'icons', t),
                    cmd=self.get_cmd('gtk3-update-icon-cache'))

    def copy_locales(self):
        mo_files=['atk10', 'gdk-pixbuf', 'glib20', 'gtk30', 'gtk30-properties']
        for l in ['zh_CN']:
            self.copy_locale(os.path.join(self.sys_root(), 'share', 'locale', l),
                    os.path.join(self.pfx, 'share', 'locale', l),
                    mo_files)
    def copy_gtk_themes(self):
        shutil.copytree(os.path.join(self.sys_root(), 'share', 'themes', 'Mac'),
                os.path.join(self.pfx, 'share', 'themes', 'Mac'),
                symlinks=False, dirs_exist_ok=True)

    @staticmethod
    def symlink_qms(d):
        for f in os.listdir(d):
            r=re.match(r'.*\b(zh_CN\.qm)$', f)
            if r:
                try:
                    os.symlink(f, os.path.join(d, f[0:r.span(1)[0]]+'zh_Hans.qm'))
                except Exception:
                    pass
                
    def run(self):
        self.symlink_qms(os.path.join(self.pfx, 'share', 'gapr', 'translations'))
        self.copy_icon_themes()
        self.copy_locales()
        self.copy_gtk_themes()

class BundlerWindows(Bundler):
    def __init__(self, argv):
        super().__init__(argv)

    def get_cmd(self, name):
        if not re.match(r'.*\.exe$', name, re.IGNORECASE):
            name+='.exe'
        if self.args.get('sys_root') is None:
            return [name]
        return ['wine', os.path.join(self.args.get('sys_root'), 'bin', name)]

    def sys_root(self):
        return self.args.get('sys_root', sys.prefix)

    def copy_icon_themes(self):
        for t in ['hicolor', 'Adwaita']:
            self.copy_icon_theme(os.path.join(self.sys_root(), 'share', 'icons', t),
                    os.path.join(self.pfx, 'share', 'icons', t),
                    cmd=self.get_cmd('gtk-update-icon-cache'))

    def copy_glib_schemas(self):
        schemas=list([
            'org.gtk.Settings.FileChooser',
            'org.gtk.Settings.ColorChooser',
            'org.gtk.Settings.EmojiChooser',
            ])
        self.copy_schema_files(os.path.join(self.sys_root(), 'share', 'glib-2.0', 'schemas'),
                os.path.join(self.pfx, 'share', 'glib-2.0', 'schemas'),
                self.get_cmd('glib-compile-schemas'), schemas)

    def copy_locales(self):
        #mo_files=['glib20', 'gtk30', 'gtk30-properties']
        #???
        mo_files=['atk10', 'gdk-pixbuf', 'glib20', 'gtk30', 'gtk30-properties']
        for l in ['zh_CN']:
            self.copy_locale(os.path.join(self.sys_root(), 'share', 'locale', l),
                    os.path.join(self.pfx, 'share', 'locale', l),
                    mo_files)

    def run(self):
        self.copy_icon_themes()
        self.copy_locales()
        self.copy_glib_schemas()

