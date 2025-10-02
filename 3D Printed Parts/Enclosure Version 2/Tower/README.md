**Description:**

The Tower component is the backbone of PiTrac.  Most of the components (cameras, strobe, and Compute Board PCBA) are mounted on the front of the Tower. The Tower consists of two parts - a Frontplate and a Backplate.  This part separation is to allow the Tower to printed with a minimum of supports.  The top part of the Case Cover (and particularly the Shank Shield risers that the shield scews into) can also act as a way of easily picking up the entire PiTrac unit.
The Tower does not include the Shank Shield, which is made from a piece of 3/16" or 1/4" polycarbonite (recommended for durability) or plexiglas.
The Frontplate and Backplates parts are each further split into two halves to allow printing.  The height would otherwise be too tall for most printers.  
As discussed in the Assembly Notes, these resulting four parts are glued and screwed together to make up the tower.
A [FreeCAD 1.0](https://www.freecad.org/downloads.php) compatible parameterized model is included here along with .stl files.

**Printing Notes:**

If you are slicing the parts yourself, you will need to manually split each of the Frontplate and Backplate.  See the [FIGURE- 'Frontplate Split Example - Prusa.png'] for how to do this on the Prusa Slicer.  Other Slicers have a similar ability to slice the parts.  
In order to result in a strong combined tower part with strong lap-joints, we suggest splitting the Frontplate 104mm up from the bottom and the Backplate up 145mm).  See the two Datum planes in the FreeCad 3D model called 'Suggested Frontplate Split DatumPlane' and 'Suggested Backplate Split DatumPlane' for confimation of these cut-planes.

PLA works well for this, but PETG probably would as well. 

Recommended print settings: 2 perimeter layers, 15% infill, grid or rectilinear infill patterns.  No supports or brims should be necessary.

**Assembly Notes:**

See the [main assembly instructions](https://github.com/pitraclm/pitrac/blob/main/Documentation/PiTrac%20Version%202%20Assembly.md).
Ultimately, the four parts will be glued together with something like thick, slower-drying super-glue such as Starbond Gap Filler Thick High Performance Super Glue and then screwed together with 
12 M2 x 6 self-tapping screws.

