"""Check the unpacked download against SHA256SUMS.txt before installing."""
import hashlib
from pathlib import Path
import sys

root=Path(__file__).resolve().parents[1]
errors=[];count=0
for line in (root/'SHA256SUMS.txt').read_text(encoding='utf-8').splitlines():
    digest,name=line.split('  ',1)
    path=(root/name).resolve()
    if not path.is_relative_to(root) or not path.is_file():
        errors.append(name+': missing or invalid path');continue
    if hashlib.sha256(path.read_bytes()).hexdigest()!=digest:
        errors.append(name+': SHA256 mismatch')
    count+=1
for error in errors:print(error)
print(f'Checked {count} files, {len(errors)} error(s).')
raise SystemExit(1 if errors else 0)
