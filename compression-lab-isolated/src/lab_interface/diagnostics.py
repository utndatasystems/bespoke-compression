"""Diagnostic verdicts must not rely on a sanitizer's recovery/exit policy."""
import re


_SANITIZER_REPORT = re.compile(
    r'runtime error:|(?:Address|UndefinedBehavior|Memory|Leak|Thread)Sanitizer:'
)


def diagnostic_failure(record):
    if _SANITIZER_REPORT.search(record.get('stderr', '')):
        return 'sanitizer_diagnostic'
    return record['reason_code'] or ('native_failed' if record['returncode'] else None)
