# StylePerFeature

StylePerFeature demonstrates styling individual features through editable attributes.

## Overview

It downloads the California sample data, copies the county polygons into an in-memory vector layer, assigns each feature a demo zone attribute, and styles the layer with a rule-based renderer. Selecting a feature from the list or clicking it on the map lets you change its zone value and immediately update its visual style.

## GIS Workflow

This example shows how to:

- Load remote vector sample data
- Copy provider data into an editable in-memory layer
- Style features from per-feature attributes
- Update a single feature’s styling interactively
- Sync map clicks with a feature list
- Refresh rendering after attribute changes
