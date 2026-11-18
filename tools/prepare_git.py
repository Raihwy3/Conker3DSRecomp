#!/usr/bin/env python3
"""Initialize and stage only the audited public source. No commit or upload."""
import subprocess
from audit_source import ROOT,audit
files=audit()
if not (ROOT/'.git').exists():subprocess.run(['git','init','-b','main',str(ROOT)],check=True)
paths=b''.join((p.relative_to(ROOT).as_posix().encode()+b'\0') for p in files)
subprocess.run(['git','-C',str(ROOT),'add','-f','--pathspec-from-file=-','--pathspec-file-nul'],input=paths,check=True)
print('Public source staged. No commit or push was made.')
