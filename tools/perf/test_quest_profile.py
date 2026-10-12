"""A stalled XR renderer remains measurable; incomplete/invalid traces fail."""
import csv
from pathlib import Path
import tempfile
import quest_profile as q

with tempfile.TemporaryDirectory(prefix='gevr-profile-validation-') as tmp:
    path = Path(tmp) / 'slow.csv'
    fields = ['kind', 'camera_valid', 'errors', 'period_ns', 'start_time_ns', 'input_buttons', 'input_turn']
    def trace(duration=60, active=1, errors=0, phases=True):
        with path.open('w', newline='') as f:
            f.write('# gevr-profile-v2 label=slow rows=1920 overflow=0 detail=0\n')
            w = csv.DictWriter(f, fieldnames=fields)
            w.writeheader()
            for i in range(1920):
                t = i * duration / 1919
                commands = [p for p in q.PHASES if p[0] <= t] if phases else []
                buttons, turn = (commands[-1][1], commands[-1][2]) if commands else (0, 0)
                w.writerow(dict(kind=1, camera_valid=active, errors=errors, period_ns=8333333,
                                start_time_ns=int(t*1e9), input_buttons=f'{buttons:x}', input_turn=turn))
    trace()
    assert q.validate(path, 120, 'done slow 1920 0') == 1920
    for kwargs in (dict(duration=10), dict(active=0), dict(errors=1), dict(phases=False)):
        trace(**kwargs)
        try:
            q.validate(path, 120, 'done slow 1920 0')
        except q.Unavailable:
            pass
        else:
            raise AssertionError(kwargs)
print('Quest trace validation accepts severe slowdown and rejects incomplete or invalid gameplay')
