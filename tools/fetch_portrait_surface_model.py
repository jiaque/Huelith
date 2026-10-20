"""Fetch the pinned CPU face-surface model; no uploads or runtime downloads."""
from pathlib import Path
import hashlib,urllib.request
ROOT=Path(__file__).resolve().parents[1]
URL="https://huggingface.co/senty-au/face_landmarks_detector-ONNX/resolve/337d58218b5b1cc597ca3c67360880b920f6ce7b/onnx/model.onnx"
SHA256="7d6e82dee82a1dca5fbddb282b3cc74571833a530de317fc22ae325c3358beeb"
target=ROOT/'research/m3/face-landmarks/face_landmarks_478.onnx'
if not target.exists() or hashlib.sha256(target.read_bytes()).hexdigest()!=SHA256:
    data=urllib.request.urlopen(URL,timeout=60).read()
    if hashlib.sha256(data).hexdigest()!=SHA256:raise RuntimeError('Face surface model checksum mismatch')
    target.parent.mkdir(parents=True,exist_ok=True)
    target.write_bytes(data)
print(target)
