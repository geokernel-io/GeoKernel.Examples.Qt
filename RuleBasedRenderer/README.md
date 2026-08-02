# RuleBasedRenderer

RuleBasedRenderer demonstrates class-based styling with explicit symbol rules.

## Overview

It downloads the USA cities sample data, loads city points, and applies a rule-based renderer using the POP_CLASS attribute. Each population class is matched by a separate rule with its own color, outline, and point size, making the relationship between attribute values and map styling clear.

## GIS Workflow

This example shows how to:

- Load remote vector sample data
- Define rule-based symbol classes
- Match features by attribute value
- Apply different point styles per rule
- Display rule classes in a legend
- Zoom the viewer to the loaded city data extent
