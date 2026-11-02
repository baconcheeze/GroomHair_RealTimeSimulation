from pathlib import Path
old=Path('diagnostics/qr-audit');new=Path('diagnostics/ls-refinement')
for name in ['Backend.h','Verify.h','Freeze.h','Equations.generated.h','Nonlinear.generated.h','QRFreeze.cpp']:
 s=(old/name).read_text(encoding='utf-8-sig')
 if name=='Freeze.h':
  begin=s.index(' bool selected=');end=s.index(' const char* path=',begin);s=s[:begin]+s[end:]
 (new/('ExportSuite.cpp' if name=='QRFreeze.cpp' else name)).write_text(s,encoding='utf-8')
