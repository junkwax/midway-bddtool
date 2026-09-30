R"BDD(
# BDDTOOL CAVE PROFILE BEGIN
import json as _bdd_json

def _bdd_fingerprint(data):
    value = 14695981039346656037
    for byte in data:
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return format(value, 'x')

_bdd_plan = _bdd_json.loads((DATA / 'MK3CAVE.bddtool.json').read_text())
if _bdd_plan.get('version') != 1:
    raise SystemExit('Unsupported bddtool cave export version')
for _bdd_relative, _bdd_expected in _bdd_plan['sources'].items():
    _bdd_path = (ROOT / _bdd_relative).resolve()
    if not _bdd_path.is_relative_to(ROOT.resolve()):
        raise SystemExit('Cave source path escapes checkout')
    if _bdd_fingerprint(_bdd_path.read_bytes()) != _bdd_expected:
        raise SystemExit('Cave export source changed: ' + _bdd_relative + '; prepare a fresh export in bddtool')
PACKS = [(name, start, end) for name, start, end, group in _bdd_plan['packs']]
PLACE = {name: tuple(place) for name, place in _bdd_plan['place'].items()}
PACK_MARGIN = 1.0

def deal_packs(imgs, order):
    groups = [group for name, start, end, group in _bdd_plan['packs']]
    flat = [i for group in groups for i in group]
    if len(flat) != len(set(flat)) or set(flat) != set(order):
        raise SystemExit('Cave export pack mapping does not cover every image exactly once')
    used = [sum(_bdd_plan['bits'][str(i)] for i in group) for group in groups]
    for (_, start, end), size in zip(PACKS, used):
        if size > (end - start) * 8:
            raise SystemExit('Cave export estimate exceeds a reserved ROM slot')
    return groups, used
# BDDTOOL CAVE PROFILE END
)BDD"
