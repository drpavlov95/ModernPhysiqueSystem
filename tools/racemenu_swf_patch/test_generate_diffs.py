from tools.racemenu_swf_patch.diff_apply import apply_diff
from tools.racemenu_swf_patch.generate_diffs import generate_diff


def test_generate_diff_round_trips_through_apply_diff():
    stock = "class RaceMenu {\n    function a() {}\n}\n"
    patched = "class RaceMenu {\n    function a() {}\n    function pwHook() {}\n}\n"
    diff = generate_diff(stock, patched, "RaceMenu")
    assert apply_diff(stock, diff) == patched
