from pathlib import Path
s=Path('diagnostics/strand-block/Equations.generated.h').read_text().replace('std::vector<std::pair<unsigned,double>> row','SmallRow row')
Path('diagnostics/native-ls/LocalEquations.generated.h').write_text(s)
