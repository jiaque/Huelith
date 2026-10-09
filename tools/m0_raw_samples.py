"""Download/verify the CC0 R10 research set without adding assets to Git."""
import argparse
import hashlib
import json
from pathlib import Path
import urllib.parse
import urllib.request


SAMPLES = {
    '5871': ('3:2', '5d2e076d38e7835d4c4f13dc2030a913cc573a31d7199b60777d23a25ccc3ef9'),
    '5873': ('3:2', 'dd33ee51dfcb142e8954e1ce375d60786459b4194968831e65d62d86d2c0a8cb'),
    '5907': ('3:2', '15d017f0770b3da17180077617ffe07b786a1ec2cb36297f81a82a27a54756db'),
    '8185': ('RAW (3:2)', '2dc0bbdf04c93ca62c914604f644c735286037912d6fba609d0cb78d00acd097'),
}


def sha256(path):
    with path.open('rb') as stream:
        return hashlib.file_digest(stream, 'sha256').hexdigest()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--directory', type=Path, default=Path('research/m0/samples'))
    parser.add_argument('--download', nargs='*', choices=SAMPLES, default=[])
    args = parser.parse_args()
    args.directory.mkdir(parents=True, exist_ok=True)
    records = []
    for identifier, (label, expected) in SAMPLES.items():
        url = f'https://raw.pixls.us/getfile.php/{identifier}/nice/' + urllib.parse.quote(
            f'Canon - EOS R10 - {label}.CR3')
        target = args.directory/f'r10-{identifier}.CR3'
        if identifier in args.download and not target.exists():
            temporary = target.with_suffix('.CR3.part')
            request = urllib.request.Request(url, headers={'User-Agent': 'Huelith-M0-research'})
            # A .part file is never treated as a valid RAW. TLS validation remains enabled.
            with urllib.request.urlopen(request, timeout=60) as response, temporary.open('wb') as output:
                while chunk := response.read(1024*1024):
                    output.write(chunk)
            if sha256(temporary) != expected:
                raise ValueError(f'checksum mismatch: {temporary}')
            temporary.replace(target)
        actual = sha256(target) if target.exists() else None
        records.append(dict(id=identifier, url=url, license='CC0-1.0',
            expected_sha256=expected, sha256=actual, verified=actual == expected,
            bytes=target.stat().st_size if target.exists() else None))
        if target.exists() and actual != expected:
            raise ValueError(f'checksum mismatch: {target}')
    (args.directory/'verified-manifest.json').write_text(json.dumps(records, indent=2), encoding='utf-8')
    print(f'{sum(r["verified"] for r in records)}/{len(records)} RAW files verified')


if __name__ == '__main__':
    main()
