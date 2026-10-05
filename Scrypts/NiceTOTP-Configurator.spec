# -*- mode: python ; coding: utf-8 -*-


from pathlib import Path

ROOT_DIR = Path(SPECPATH).parent.parent

a = Analysis(
    [str(ROOT_DIR / 'App' / 'NiceTOTP-ConfiguratorNext.py')],
    pathex=[str(ROOT_DIR / 'App')],
    binaries=[],
    datas=[
        (str(ROOT_DIR / 'App' / 'NiceTOTP-ConfiguratorNext.qml'), '.'),
        (str(ROOT_DIR / 'App' / 'icon.webp'), '.'),
    ],
    hiddenimports=['migration_payload_pb2'],
    hookspath=[],
    hooksconfig={},
    runtime_hooks=[],
    excludes=[],
    noarchive=False,
    optimize=0,
)
pyz = PYZ(a.pure)

exe = EXE(
    pyz,
    a.scripts,
    a.binaries,
    a.datas,
    [],
    name='NiceTOTP-Configurator',
    debug=False,
    bootloader_ignore_signals=False,
    strip=False,
    upx=True,
    upx_exclude=[],
    runtime_tmpdir=None,
    console=False,
    disable_windowed_traceback=False,
    argv_emulation=False,
    target_arch=None,
    codesign_identity=None,
    entitlements_file=None,
)
