"""Author a two-title unencrypted DVD and ISO; never use/mount an optical drive."""
import argparse
import subprocess
from pathlib import Path
from xml.etree import ElementTree as ET

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("directory")
parser.add_argument("--dvdauthor", default="dvdauthor")
parser.add_argument("--genisoimage", default="genisoimage")
args = parser.parse_args()
root = Path(args.directory).resolve()
root.mkdir(parents=True, exist_ok=True)
for number, duration in [(1, 6), (2, 12)]:
    subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "lavfi", "-i",
                    "testsrc2=size=720x576:rate=25" if number == 1 else "color=c=blue:size=720x576:rate=25",
                    "-f", "lavfi", "-i", f"sine=frequency={440 * number}:sample_rate=48000",
                    "-t", str(duration), "-target", "pal-dvd", "-c:a", "ac3", "-ac", "2",
                    "-threads", "2", str(root / f"title{number}.mpg")], check=True)
dvd = ET.Element("dvdauthor", {"dest": str(root / "DVD")})
manager = ET.SubElement(dvd, "vmgm")
menus = ET.SubElement(manager, "menus")
ET.SubElement(menus, "video", {"format": "pal"})
for number, chapter in [(1, 3), (2, 6)]:
    titles = ET.SubElement(ET.SubElement(dvd, "titleset"), "titles")
    ET.SubElement(titles, "video", {"format": "pal"})
    ET.SubElement(titles, "audio", {"format": "ac3", "lang": "en"})
    program = ET.SubElement(titles, "pgc")
    ET.SubElement(program, "vob", {"file": str(root / f"title{number}.mpg"), "chapters": f"0,{chapter}"})
    ET.SubElement(program, "post").text = "exit;"
config = root / "dvd.xml"
ET.ElementTree(dvd).write(config, encoding="utf-8")
subprocess.run([args.dvdauthor, "-x", str(config)], check=True)
subprocess.run([args.genisoimage, "-quiet", "-dvd-video", "-udf", "-o", str(root / "test-dvd.iso"), str(root / "DVD")], check=True)
