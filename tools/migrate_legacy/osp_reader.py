import xml.etree.ElementTree as ET
from dataclasses import dataclass, field
from pathlib import Path


@dataclass
class ShapeInfo:
    name: str
    target: str


@dataclass
class SliderInfo:
    name: str
    data_files: list[str] = field(default_factory=list)


@dataclass
class SliderSetInfo:
    name: str
    group: str
    data_folder: str
    source_file: str
    output_path: str
    shapes: list[ShapeInfo] = field(default_factory=list)
    sliders: list[SliderInfo] = field(default_factory=list)

    def osd_filename(self) -> str:
        return Path(self.source_file).stem + ".osd"

    def slider_names(self) -> set[str]:
        return {s.name for s in self.sliders}


def read_osp(path: Path) -> list[SliderSetInfo]:
    try:
        tree = ET.parse(path)
        root = tree.getroot()
    except ET.ParseError:
        return []
    results = []
    for ss in root.findall("SliderSet"):
        info = SliderSetInfo(
            name=ss.get("name", ""),
            group=ss.findtext("Group", ""),
            data_folder=ss.findtext("DataFolder", ""),
            source_file=ss.findtext("SourceFile", ""),
            output_path=ss.findtext("OutputPath", ""),
        )
        for shape_elem in ss.findall("Shape"):
            info.shapes.append(ShapeInfo(
                name=shape_elem.text or "",
                target=shape_elem.get("target", ""),
            ))
        for slider_elem in ss.findall("Slider"):
            s = SliderInfo(name=slider_elem.get("name", ""))
            for data_elem in slider_elem.findall("Data"):
                s.data_files.append(data_elem.text or "")
            info.sliders.append(s)
        results.append(info)
    return results
