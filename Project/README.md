# Project

Project demonstrates how to load a complete GeoKernel project file with its saved layers, styles, view extent, and rendering settings.

## Overview

It prepares the required Andalucia sample project data from the remote sample-data release, opens `andalucia.geokernel`, reports loading progress, and waits for the first completed map render before resetting the progress indicator.

## GIS Workflow

This example shows the project-based GIS workflow:

- Download and prepare project sample data
- Open a saved `.geokernel` project
- Restore layers and symbology
- Track project loading progress
- Present the restored map in an interactive viewer
