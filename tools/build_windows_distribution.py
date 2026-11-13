"""Wrap an already validated portable package in a ZIP and Inno Setup installer."""
import argparse
import hashlib
from pathlib import Path
import subprocess
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--iscc', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--version', default='0.1')
    args = parser.parse_args()
    package, output = args.package.resolve(), args.output.resolve()
    if output == package or output.is_relative_to(package):
        raise ValueError('Output must be outside the source package')
    for required in ('Huelith.exe', 'LICENSE', 'Qt6Core.dll', 'platforms/qwindows.dll'):
        if not (package / required).is_file():
            raise FileNotFoundError(package / required)
    files = sorted(p for p in package.rglob('*') if p.is_file())
    symbols = [p for p in files if p.suffix.lower() in
               {'.pdb', '.ilk', '.obj', '.exp', '.lib', '.debug'}]
    if symbols:
        raise ValueError(f'Debug/build artifacts found: {symbols}')
    output.mkdir(parents=True, exist_ok=True)
    base = f'Huelith-v{args.version}-Setup'
    artifacts = [output / (base + ext) for ext in ('.exe', '.zip')]
    if any(p.exists() for p in artifacts):
        raise FileExistsError('Choose a fresh output directory or version')
    script = Path(__file__).parent / 'installer/Huelith.iss'
    subprocess.run([str(args.iscc.resolve()), f'/DPackageDir={package}',
                    f'/DOutputDir={output}', f'/DAppVersion={args.version}',
                    str(script.resolve())], check=True)
    with zipfile.ZipFile(artifacts[1], 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as archive:
        for file in files:
            archive.write(file, f'Huelith-v{args.version}/{file.relative_to(package).as_posix()}')
    with zipfile.ZipFile(artifacts[1]) as archive:
        failed = archive.testzip()
        if failed:
            raise ValueError(f'ZIP checksum failed: {failed}')
    checksums = []
    for artifact in artifacts:
        with artifact.open('rb') as stream:
            digest = hashlib.file_digest(stream, 'sha256').hexdigest()
        checksums.append(f'{digest}  {artifact.name}\n')
        print(f'{artifact}: {artifact.stat().st_size / 1024**2:.1f} MiB')
    (output / 'SHA256SUMS.txt').write_text(''.join(checksums), encoding='ascii')


if __name__ == '__main__':
    main()
