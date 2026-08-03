# WktRoundtrip

WktRoundtrip demonstrates how to read geometry from Well-Known Text and serialize it back to WKT with GeoKernel.

## Overview

It parses a polygon WKT string with `GisWktReader`, adds the resulting geometry to an in-memory polygon layer, and writes the same shape back to text with `GisWktWriter`. The map displays the parsed polygon while the details panel presents the original and generated WKT values for direct comparison.

## GIS Workflow

This example shows how to:

- Read polygon geometry from a WKT string
- Create an in-memory polygon layer
- Add parsed geometry to an editable layer
- Display the reconstructed geometry in the viewer
- Serialize a GeoKernel polygon back to WKT
- Compare input and output WKT representations
- Verify a basic WKT round-trip workflow
- Inspect the reader and writer APIs together
