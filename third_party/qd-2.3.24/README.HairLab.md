# QD 2.3.24 (diagnostic residual arithmetic only)

Pinned upstream tag: https://github.com/BL-highprecision/QD/tree/v2.3.24
Raw source: https://raw.githubusercontent.com/BL-highprecision/QD/v2.3.24/

Vendored dd_real.h, dd_inline.h, inline.h are upstream source with normalized
line endings. LICENSE (BSD-3-Clause) and COPYING are retained. No math body
was rewritten. qd_config.h is this project's static x64 configuration
(QD_INLINE, QD_IEEE_ADD); fpu.h is an empty platform adapter because this
x64/SSE2-only diagnostic does not call x87 precision-control functions.

Nominal double-double significand: about 106 bits. Upstream numeric_limits
conservatively reports 104; the executable records both this value and an
actual 1 + 2^-105 cancellation probe. This is software arithmetic, not an
IEEE binary128 type or assumed hardware acceleration. Only residual products
and transpose accumulation use this type. No transcendental DD routines or
out-of-line QD runtime are used. C++ compilation uses /fp:precise.

File SHA256 values are recorded by diagnostics/native-ls/summarize.py and in
the run artifact manifest. The dependency is vendored: configure/build/run
requires no network and no installed QD package.
