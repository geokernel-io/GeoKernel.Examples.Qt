# AddWithAttributes

AddWithAttributes demonstrates how to create point features together with attribute values in GeoKernel.

## Overview

It prepares the required world sample data, loads a reference map layer, and creates an editable in-memory point layer with custom attribute definitions. Each Add Point action inserts a generated coordinate and a `QHash` of attribute values, while the table and Info tool let the user inspect the stored feature data interactively.

## GIS Workflow

This example shows how to:

- Create an editable in-memory point layer
- Define custom attribute fields
- Add point geometry and attributes together
- Store feature values with a `QHash`
- Display added attributes in a table
- Identify a feature and read its attributes
- Clear temporary features and refresh the map
