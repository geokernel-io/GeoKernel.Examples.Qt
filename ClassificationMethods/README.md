# ClassificationMethods

ClassificationMethods demonstrates how different classification strategies affect graduated map styling.

## Overview

It downloads the California sample data, loads county polygons, and applies a graduated renderer to the POPULATION attribute. The toolbar lets you switch between classification methods such as Equal Interval, Quantile, and Standard Deviation, while the legend updates to show the resulting class breaks.

## GIS Workflow

This example shows how to:

- Load remote vector sample data
- Create a graduated renderer from a numeric attribute
- Compare different classification methods
- Update renderer rules interactively
- Refresh the legend when classification changes
- Zoom the viewer to the loaded layer extent
