# LayerStylingAndFiltering

Example 15: style and filter roads and extruded building footprints over terrain.

Uses the standard SDK acquisition and runtime helpers. CMake acquires the latest
public SDK unless GEOKERNEL_ROOT points to a local SDK. GeoKernel::Viewer3D is
required; a 2D-only public package cannot build this example.

The first launch downloads the following assets from
https://github.com/geokernel-io/GeoKernel.SampleData/releases/download/v1/ :
- sagrada_familia_terrain.zip
- sagrada_familia_ortophoto.zip
- sagrada_familia_roads.zip
- sagrada_familia_buildings.zip

Archives are extracted and cached under data beside the executable. All four
layers open automatically; a loading page remains visible until the scene is
ready. Reload reuses cached files and resets the loaded scene settings.

Select Roads or Buildings in the right panel. Each layer retains independent
visibility, opacity, uniform/source color, attribute coloring and text filters.
Choose Color by attribute for SDK category colors. Choosing a uniform color or
Restore source color clears that layer's category theme. Category colors are
categorical, not a continuous numeric scale.

Filter attributes come from the loaded features. Contains uses literal,
case-insensitive substring matching (not SQL or a regular expression). An empty
query matches everything. All attributes / source IDs also searches source IDs
and source FIDs. Example values lists up to 200 distinct values; you can type
any text and filtering always searches the full layer. Changes apply after a
short typing delay and pending edits are preserved when switching layers.

The panel reports matching source records and drawable matching ranges. Hidden
or transparent layers can still have matches. Filters affect both drawing and
picking. Zoom to matching features requires a visible layer with matching
geometry. Clear this filter preserves styling; Reset all restores both vector
layers' original styles and clears their filters. Source files are never edited.
Settings are session-only; project persistence is covered by another example.

The left panel controls orthophoto visibility and vertical exaggeration. DEM
vertical datum is unverified; buildings use illustrative 9 m heights. These are
footprint extrusions, not the Barcelona 3D Tiles or textured landmark models.

Build using windows-msvc and windows-msvc-release CMake presets, supplying Qt
and optionally GEOKERNEL_ROOT. Output:
outputs/windows/Release/bin/LayerStylingAndFiltering.exe.

Release build passed using the local 3D SDK. The graphical application was not
launched; interactive appearance/filtering and first-download acceptance remain
to be checked.
