"""Port only Physique Wheel UI integration onto the exported AE class.

Usage: python port_ae_overlay.py <AE scripts/__Packages> <output directory>
Native AE slider creation/recovery stays native: SE private SKEE offsets must
never be introduced by copying the entire SE RaceMenu class onto AE.
"""
from pathlib import Path
import re
import sys


def method(source, name):
    pattern = rf"(?ms)^   function {re.escape(name)}\([^\n]*\)\n   \{{.*?^   \}}\n"
    match = re.search(pattern, source)
    if not match:
        raise ValueError(f"Missing method {name}")
    return match.group()


def update(source, name, transform):
    old = method(source, name)
    new = transform(old)
    assert old != new, name
    return source.replace(old, new, 1)


def append_body(body, addition):
    assert body.endswith("   }\n")
    return body[:-5] + addition + "   }\n"


def port(ae_dir, output):
    se = Path("reference/RaceMenu/patched/RaceMenu.as").read_text()
    stock = (ae_dir / "RaceMenu.as").read_text()
    source = stock.replace("   function RaceMenu(",
        "   var bPhysiqueBodyCategoryActive = false;\n"
        "   var bPhysiqueSlidersModeActive = true;\n"
        "   var physiqueTitle = null;\n"
        "   function RaceMenu(", 1)
    assert source != stock
    for name in ("HideTextEntryWithoutRestoringPanels", "getWeightSliderEntry", "getPhysiqueTitleAnchorClip", "refreshPhysiqueTitleRow",
                 "ensurePhysiqueTitle", "updatePhysiqueTitleVisibility",
                 "isPhysiqueWeightSyncContextActive", "syncPhysiqueOverlayState"):
        body = method(se, name)
        if name == "isPhysiqueWeightSyncContextActive":
            body = body.replace("!this.makeupPanel.enabled;", "!this.makeupPanel.enabled && !this.partsPanel.enabled;")
        source = source[:-2] + body + "}\n"
    source = update(source, "onLoad", lambda b: append_body(b,
        "      this.ensurePhysiqueTitle();\n      this.updatePhysiqueTitleVisibility(false);\n"))
    # Keep native AE callbacks and GetSliderPartData; omit only the Weight row.
    source = update(source, "SetSliders", lambda b: b.replace(
        '         if(entryObject.callbackName == "ChangeTintingMask"',
        '         if(entryObject.callbackName == "ChangeWeight")\n'
        '         {\n            _loc5_ += RaceMenuDefines.SLIDER_STRIDE;\n            continue;\n         }\n'
        '         if(entryObject.callbackName == "ChangeTintingMask"', 1))
    press = method(se, "onCategoryPress")
    press = re.sub(r"^\s*this\.(?:start|stop)PhysiqueWeightSync\(\);\n", "", press, flags=re.M)
    press = press.replace("         else\n         {\n         }\n", "")
    source = update(source, "onCategoryPress", lambda _: press)
    for name in ("ShowTextEntry", "ShowColorField", "ShowMakeupPanel", "ShowPartsPanel"):
        source = update(source, name, lambda b: append_body(b, "      this.syncPhysiqueOverlayState();\n"))
    source = update(source, "ShowRacePanel", lambda b: b.replace("   {\n",
        "   {\n      this.updatePhysiqueTitleVisibility(bShowPanel);\n", 1))
    source = update(source, "onChangeMode", lambda b: b.replace("   {\n",
        "   {\n      this.bPhysiqueSlidersModeActive = event.index == 0;\n      this.syncPhysiqueOverlayState();\n", 1))
    hide = ('      if(_global.skse.plugins.ModernPhysiqueSystem.SetActiveCategory)\n'
            '      {\n         _global.skse.plugins.ModernPhysiqueSystem.SetActiveCategory(0,"");\n      }\n')
    source = update(source, "onDoneClicked", lambda b: b.replace(
        '      gfx.io.GameDelegate.call("ConfirmDone",[]);', hide + '      gfx.io.GameDelegate.call("ConfirmDone",[]);'))
    source = update(source, "onNameChange", lambda _: method(se, "onNameChange"))
    transition = ('         if(_global.skse.plugins.ModernPhysiqueSystem.PrepareNativeFaceTransition)\n'
                  '         {\n            _global.skse.plugins.ModernPhysiqueSystem.PrepareNativeFaceTransition();\n         }\n')
    source = update(source, "onChangeRaceClicked", lambda b: b.replace(
        '         gfx.io.GameDelegate.call("ChangeRace",', transition + '         gfx.io.GameDelegate.call("ChangeRace",', 1))
    # A synchronous notification restores only PW-owned geometry before AE
    # rebuilds it. This neither repairs nor replaces SKEE's private slider map.
    slider = (ae_dir / "SliderListEntry.as").read_text()
    slider = slider.replace('         gfx.io.GameDelegate.call(this.callbackName,[this.position,this.sliderID]);',
        '         if((this.callbackName == "ChangeSex" || this.callbackName == "ChangeRace") && _global.skse.plugins.ModernPhysiqueSystem.PrepareNativeFaceTransition)\n'
        '         {\n            _global.skse.plugins.ModernPhysiqueSystem.PrepareNativeFaceTransition();\n         }\n'
        '         gfx.io.GameDelegate.call(this.callbackName,[this.position,this.sliderID]);', 1)
    assert "PrepareFaceMap" not in source + slider
    assert method(stock, "InitializeSliders") == method(source, "InitializeSliders")
    assert method(stock, "onChangePart") == method(source, "onChangePart")
    assert "entryObject.GetSliderPartData" in source
    output.mkdir(parents=True, exist_ok=True)
    (output / "RaceMenu.as").write_text(source, newline="\n")
    (output / "SliderListEntry.as").write_text(slider, newline="\n")


if __name__ == "__main__":
    port(Path(sys.argv[1]), Path(sys.argv[2]))
