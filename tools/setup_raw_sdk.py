"""Fetch pinned official native RAW inference dependencies with SHA-256 verification."""
from pathlib import Path
import hashlib
import urllib.request
import zipfile

ROOT = Path(__file__).resolve().parents[1]
PACKAGES = (
    ("microsoft.ml.onnxruntime.directml", "1.24.4",
     "57e9f11b73437bef7a309496135d4c1f96b1a8e9ddba60013fa27bfc1d788681"),
    ("microsoft.ai.directml", "1.15.4",
     "4e7cb7ddce8cf837a7a75dc029209b520ca0101470fcdf275c1f49736a3615b9"),
)


def main():
    cache = ROOT / "build/dependencies"
    destination = cache / "onnxruntime-directml"
    destination.mkdir(parents=True, exist_ok=True)
    for name, version, expected in PACKAGES:
        package = cache / f"{name}.{version}.nupkg"
        if not package.exists():
            url = f"https://api.nuget.org/v3-flatcontainer/{name}/{version}/{package.name}"
            temporary = package.with_suffix(".download")
            print(f"Downloading {name} {version}", flush=True)
            with urllib.request.urlopen(url, timeout=60) as response, temporary.open("wb") as file:
                while block := response.read(1024 * 1024):
                    file.write(block)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != expected:
                raise ValueError(f"SHA-256 mismatch: {temporary}")
            temporary.replace(package)
        if hashlib.sha256(package.read_bytes()).hexdigest() != expected:
            raise ValueError(f"SHA-256 mismatch: {package}")
        with zipfile.ZipFile(package) as archive:
            for member in archive.namelist():
                path = Path(member)
                if name == "microsoft.ai.directml":
                    include = member in ("include/DirectML.h", "bin/x64-win/DirectML.dll")
                else:
                    include = ((member.startswith("build/native/include/") and path.suffix == ".h")
                               or (member.startswith("runtimes/win-x64/native/")
                                   and path.suffix in (".dll", ".lib")))
                if include:
                    (destination / path.name).write_bytes(archive.read(member))
                if path.name.lower() in ("license.txt", "license", "thirdpartynotices.txt"):
                    (destination / f"{name}-{path.name}").write_bytes(archive.read(member))
        print(f"Verified and extracted {name} {version}", flush=True)


if __name__ == "__main__":
    main()
