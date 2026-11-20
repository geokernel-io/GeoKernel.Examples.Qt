"""Generate the synthetic neighbourhood used by the AgentMapAssistant examples.

Deterministic: the same files on every run. Output: <out>/{streets,buildings,schools,parks}.geojson
"""
import json
import math
import random
import sys
from pathlib import Path

out = Path(sys.argv[1])
out.mkdir(parents=True, exist_ok=True)
rng = random.Random(20261121)

# Local metric grid around (29.0300 E, 40.9850 N); 1 m in degrees at this latitude.
LON0, LAT0 = 29.0300, 40.9850
M_LAT = 1.0 / 111_132.0
M_LON = 1.0 / (111_320.0 * math.cos(math.radians(LAT0)))


def ll(x, y):
    return [round(LON0 + x * M_LON, 7), round(LAT0 + y * M_LAT, 7)]


def ring(x0, y0, x1, y1):
    return [ll(x0, y0), ll(x1, y0), ll(x1, y1), ll(x0, y1), ll(x0, y0)]


def collection(features):
    return {"type": "FeatureCollection", "features": features}


COLS, ROWS = 8, 6
BLOCK_W, BLOCK_H, STREET = 110.0, 90.0, 16.0
WIDTH = COLS * BLOCK_W + (COLS + 1) * STREET
HEIGHT = ROWS * BLOCK_H + (ROWS + 1) * STREET
X0, Y0 = -WIDTH / 2, -HEIGHT / 2

AVENUES = ["Bahariye Avenue", "Moda Avenue", "Kadife Street", "Sogutlucesme Avenue", "Muhurdar Street",
           "Yeldegirmeni Street", "Rihtim Avenue", "Caferaga Street", "Iskele Street"]
STREETS = ["Lale Street", "Nergis Street", "Papatya Street", "Menekse Street", "Gul Street",
           "Zambak Street", "Sumbul Street"]

streets = []
for c in range(COLS + 1):
    x = X0 + c * (BLOCK_W + STREET) + STREET / 2
    primary = c == 4
    streets.append({"type": "Feature",
                    "properties": {"name": AVENUES[c], "class": "primary" if primary else "residential",
                                   "lanes": 4 if primary else 2},
                    "geometry": {"type": "LineString", "coordinates": [ll(x, Y0), ll(x, Y0 + HEIGHT)]}})
for r in range(ROWS + 1):
    y = Y0 + r * (BLOCK_H + STREET) + STREET / 2
    streets.append({"type": "Feature",
                    "properties": {"name": STREETS[r], "class": "secondary" if r == 3 else "residential",
                                   "lanes": 3 if r == 3 else 2},
                    "geometry": {"type": "LineString", "coordinates": [ll(X0, y), ll(X0 + WIDTH, y)]}})


def block_origin(c, r):
    return X0 + STREET + c * (BLOCK_W + STREET), Y0 + STREET + r * (BLOCK_H + STREET)


PARK_BLOCKS = {(1, 4): "Yogurtcu Park", (6, 1): "Moda Park", (3, 0): "Fenerbahce Garden"}
SCHOOL_BLOCKS = {(2, 2): ("Moda Primary School", "primary", 420),
                 (5, 4): ("Bahariye Secondary School", "secondary", 610),
                 (6, 0): ("Caferaga High School", "high", 780),
                 (0, 5): ("Yeldegirmeni Primary School", "primary", 350)}

parks, schools, buildings = [], [], []
building_id = 0
for r in range(ROWS):
    for c in range(COLS):
        bx, by = block_origin(c, r)
        if (c, r) in PARK_BLOCKS:
            inset = 4.0
            parks.append({"type": "Feature",
                          "properties": {"name": PARK_BLOCKS[(c, r)], "trees": rng.randint(60, 240)},
                          "geometry": {"type": "Polygon",
                                       "coordinates": [ring(bx + inset, by + inset, bx + BLOCK_W - inset, by + BLOCK_H - inset)]}})
            continue
        # Two rows of footprints per block, 3-5 per row.
        for row in range(2):
            count = rng.randint(3, 5)
            depth = rng.uniform(26, 36)
            y0 = by + 3 if row == 0 else by + BLOCK_H - 3 - depth
            widths = [rng.uniform(0.8, 1.2) for _ in range(count)]
            scale = (BLOCK_W - 6 - 3 * (count - 1)) / sum(widths)
            x = bx + 3
            for i in range(count):
                w = widths[i] * scale
                school_here = (c, r) in SCHOOL_BLOCKS and row == 0 and i == 0
                building_id += 1
                if school_here:
                    name, level, students = SCHOOL_BLOCKS[(c, r)]
                    use, floors, year, status = "public", rng.randint(2, 4), rng.randint(1960, 2005), "in use"
                    lon, lat = ll(x + w / 2, y0 + depth / 2)
                    schools.append({"type": "Feature",
                                    "properties": {"name": name, "level": level, "students": students},
                                    "geometry": {"type": "Point", "coordinates": [lon, lat]}})
                else:
                    use = rng.choices(["residential", "commercial", "mixed"], [6, 2, 2])[0]
                    if c == 4 or c == 3:
                        use = rng.choices(["commercial", "mixed", "residential"], [4, 3, 2])[0]
                    floors = rng.choices(range(1, 13), [3, 5, 8, 9, 9, 7, 5, 4, 3, 2, 2, 1])[0]
                    year = rng.randint(1950, 2023)
                    status = rng.choices(["in use", "vacant", "demolished"], [90, 7, 3])[0]
                buildings.append({"type": "Feature",
                                  "properties": {"ref": f"B{building_id:03d}", "use": use, "floors": floors,
                                                 "year_built": year, "status": status},
                                  "geometry": {"type": "Polygon", "coordinates": [ring(x, y0, x + w, y0 + depth)]}})
                x += w + 3

for name, features in (("streets", streets), ("buildings", buildings), ("schools", schools), ("parks", parks)):
    (out / f"{name}.geojson").write_text(json.dumps(collection(features), separators=(",", ":")) + "\n", encoding="utf-8")

print(f"{len(buildings)} buildings, {len(schools)} schools, {len(parks)} parks, {len(streets)} streets; "
      f"demolished={sum(b['properties']['status'] == 'demolished' for b in buildings)}, "
      f"before1975={sum(b['properties']['year_built'] < 1975 for b in buildings)}, "
      f"tall={sum(b['properties']['floors'] > 8 for b in buildings)}")
