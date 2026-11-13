# Cubra how-to manual

<!--
This file is built into Cubra. The AI assistant answers questions from it, and the
on-screen walkthrough points at the names in {braces}.

Each topic starts with "## Title", then:
  id:        a short unique name
  keywords:  words and phrases people might use when asking
  also:      (optional) interface names the topic covers without a step of their own
then a short description, numbered steps, and optional "Tip:" lines.

A step's {name} is the button, box or panel the walkthrough points at: a widget or
action's objectName, or {viewport} for the 3D view. Steps without one get no arrow.

The interface test checks that every {name} still exists, and that every toolbar
button and menu item is covered by some topic. Add or rename a tool, and the test
fails until this file is updated.
-->

## Find your way around
id: overview
keywords: start, begin, beginner, new user, interface, layout, where is, toolbar, screen, tour, getting started, basics
Cubra is a CAD program for 3D printing. You build a design from solid shapes, shape them with tools, and export a file for your slicer.

1. The toolbar along the top holds every tool. Point at a button to see its list. {mainToolbar}
2. The big 3D view is the print bed. Click solids here to select them. {viewport}
3. The view cube turns the view. Click a face of it to look from that side. {viewCube}
4. The bar under the cube moves the view and changes what is shown. {navigationBar}
5. Snap and grid settings are at the bottom right. {precisionBar}
6. The blue tab on the right edge opens the parts drawer: shapes, hardware and colours. {drawerTab}

Tip: right-click a solid, a face or an edge for a menu of what you can do with it.

## Add a shape
id: add-shape
keywords: add, shape, primitive, box, cube, cylinder, cone, sphere, ball, torus, ring, wedge, prism, hexagon, star, gear, spring, coil, place, insert, new solid
Adds a box, cylinder, sphere or other basic solid to the design.

1. Point at the shapes button to open its list. {primitivesButton}
2. Click the shape you want, such as Box. {primitivesFlyout}
3. Move the pointer over the bed. The shape follows it and settles on the grid or on the face under it. {viewport}
4. Type the sizes in the boxes beside the shape. Tab moves to the next box. {placementHud}
5. Press Enter, or click, to place it. Esc cancels.

Tip: drag the arrows on the shape to size it by hand. Shift-drag an arrow to scale it evenly.

## Use the parts drawer
id: parts-drawer
keywords: drawer, parts drawer, hardware, nut, bolt, washer, screw, ready made, library, catalogue, catalog, standard parts
The parts drawer holds shapes, nuts, bolts, washers, gears and colours.

1. Click the tab on the right edge of the 3D view. {drawerTab}
2. Click a part to place it, or drag it onto the bed. {partsDrawer}
3. Place it the same way as a shape: sizes in the boxes, then Enter. {placementHud}

## Colour a part
id: colour
keywords: colour, color, paint, material, appearance, red, blue, green
Gives a part a colour on screen. Colours help you tell parts apart; they are not exported.

1. Click the part to select it. {viewport}
2. Open the parts drawer with the tab on the right edge. {drawerTab}
3. Click a colour, or drag a colour onto any part. {partsDrawer}

Tip: right-click a part and choose Material… to pick any colour.

## Select solids, faces and edges
id: select
keywords: select, selection, pick, choose, click, highlight, face, edge, corner, point, vertex, multiple, several, shift, box select, drag select, deselect
A first click selects a whole solid. A second click on it selects the face, edge or corner under the pointer.

1. Click a solid to select it. Click it again to select the face, edge or corner under the pointer. {viewport}
2. Hold Shift and click to add more. {viewport}
3. To select by dragging a box, turn on Select, then drag over the parts. {selectAction}
4. Press Esc, or click empty space, to clear the selection.

Tip: Ctrl+A selects everything. Double-click a group to work on the parts inside it.

## Move or rotate a part
id: move
keywords: move, rotate, turn, spin, position, place, shift, translate, orientation, angle, flip, reposition
Moves a part and turns the part itself, with arrows and rings you drag or numbers you type. To turn your view of the workspace instead, see "Turn the workspace view".

1. Click the part to select it. {viewport}
2. Point at Transform to open its list. {transformButton}
3. Click Move / Rotate. {menu_move}
4. Drag an arrow to slide the part, or a ring to turn it, or type the distances and angles in the boxes. {transformHud}
5. Press Enter to apply, or Esc to cancel.

Tip: press M instead of using the menu. Drags step by the snap size at the bottom right.

## Scale a part
id: scale
keywords: scale, resize, size, bigger, smaller, larger, enlarge, shrink, grow, percent, stretch, proportion
Makes a part bigger or smaller.

1. Click the part to select it. {viewport}
2. Point at Transform to open its list. {transformButton}
3. Click Scale for even scaling, Non-Uniform Scale to stretch along one direction, or Smart Scale to type exact sizes. {menu_scale}
4. Drag the handle or type the values in the boxes. {transformHud}
5. Press Enter to apply.
also: menu_nonuniform, menu_smart

Tip: press S to start Scale.

## Line parts up (Align)
id: align
keywords: align, line up, centre, center, flush, level, even, match, arrange, side by side
Lines one part up against another: flush with a side, or centred.

1. Point at Transform to open its list. {transformButton}
2. Click Align. {menu_align}
3. Click the part to move (Object A), then the part to line it up with (Object B). {stepBar}
4. In the Align window, choose a side or the centre for each direction. The part moves as you choose. Click OK.

## Put a part on the bed (Drop)
id: drop
keywords: drop, bed, floor, ground, lay flat, sit, rest, floating, hover, down, z zero
Lowers the selected parts until they rest on the bed, so they print.

1. Click the part to select it. {viewport}
2. Press D, or right-click it and choose Drop to the bed.

## Push or pull a face (Press Pull)
id: press-pull
keywords: press pull, push, pull, extrude, thicker, thinner, taller, shorter, longer, lengthen, extend, face, offset
Pushes a flat face in or pulls it out, making the part thinner or thicker.

1. Point at Modify to open its list. {modifyButton}
2. Click Press Pull. {menu_presspull}
3. Click the flat face to move. {viewport}
4. Drag the arrow, or type the distance in the box. A negative distance pushes the face in. {modifyHud}
5. Press Enter to apply, or Esc to cancel.

Tip: select a face and press P to start straight away. Press Pull works on flat faces only.

## Round an edge (Fillet)
id: fillet
keywords: fillet, round, rounded, rounding, smooth, soften, curve, radius, edge, corner, sharp, blunt, ball corner
Rounds sharp edges, or corners, with the radius you choose.

1. Point at Modify to open its list. {modifyButton}
2. Click Fillet. {menu_fillet}
3. Click the edge to round. Shift-click more edges to round them together. {viewport}
4. Drag the arrow, or type the radius in the box. {modifyHud}
5. Press Enter to apply, or Esc to cancel.

Tip: select edges first and press F. Select a corner point instead of an edge to round just that corner.

## Bevel an edge (Chamfer)
id: chamfer
keywords: chamfer, bevel, angle, cut corner, slope, edge, sharp, elephant foot, 45 degrees
Cuts a flat slope across an edge.

1. Point at Modify to open its list. {modifyButton}
2. Click Chamfer. {menu_chamfer}
3. Click the edge to bevel. Shift-click more edges to bevel them together. {viewport}
4. Drag the arrow, or type the distance in the box. {modifyHud}
5. Press Enter to apply, or Esc to cancel.

Tip: chamfer the bottom edges of a part rather than rounding them: a chamfer prints without overhang and hides the slight bulge of the first layer.

## Cut a part in two (Split Solid)
id: split-solid
keywords: split, cut, slice, divide, halve, in half, two pieces, separate along, section
Cuts a solid into two along the plane of a flat face.

1. Point at Modify to open its list. {modifyButton}
2. Click Split Solid. {menu_splitsolid}
3. Click the solid to split, then a flat face whose plane passes through it. {stepBar}

Tip: to cut at a set height, add a thin box at that height as a cutting face, split, then delete the box.

## Tools that aren't built yet
id: not-built
keywords: tweak, split face, shell, hollow, joint, hinge, sketch, sketch on face, hide sketches, not working, missing, unavailable, coming soon
Tweak, Split Face, Shell, Joint and Sketch on Face open their steps so you can see how they will work, but they don't change the design yet.

1. They are in the Modify list (Tweak, Split Face, Shell) and the Snap list (Joint). {modifyButton}
also: menu_tweak, menu_splitface, menu_shell, menu_joint, vis_hidesketches

Tip: to hollow a part now, subtract a slightly smaller copy of it. See "Make a hole or cut-out".

## Make a hole or cut-out (Subtract)
id: subtract
keywords: hole, through, subtract, cut out, cutout, remove, drill, bore, pocket, recess, hollow, slot, minus, difference, boolean, through hole
Cuts one solid out of another: add a shape where the hole should be, then subtract it.

1. Add a shape the size of the hole, such as a cylinder, and place it through the part. {primitivesButton}
2. Point at Combine to open its list. {combineButton}
3. Click Subtract. {menu_subtract}
4. Click the solid to keep, then the cutting solid. {stepBar}
5. Press Enter to apply. {stepApply}

Tip: for a hole that goes all the way through, make the cutter a little taller than the part. For a bolt to fit, make the hole 0.2 to 0.4 mm wider than the bolt.

## Join, intersect or separate solids (Combine)
id: combine
keywords: combine, merge, join, into one, one part, together, union, unite, weld, fuse, intersect, overlap, common, separate, loose pieces, boolean, one piece
Merge joins solids into one. Intersect keeps only where they overlap. Separate splits loose pieces into their own solids.

1. Point at Combine to open its list. {combineButton}
2. Click Merge, Intersect or Separate. {menu_merge}
3. Click the solid to keep, then the others. {stepBar}
4. Press Enter to apply. {stepApply}
also: menu_intersect, menu_separate

Tip: on export, all solids are fused into one piece anyway. Merge them when you want to treat them as one part while designing.

## Snap one part onto another
id: snap
keywords: snap, attach, stick, place on, put on, mate, join faces, face to face, stack, mount
Moves a part so one of its faces sits flat against a face of another part.

1. Point at Snap to open its list. {snapButton}
2. Click Snap. {menu_snap}
3. Click the face on the part to move, then the face to snap it onto. {stepBar}

## Group parts
id: group
keywords: group, ungroup, ungroup all, together, bundle, assembly, combine selection, keep together
A group moves and selects as one, while its parts stay separate solids.

1. Select two or more solids: Shift-click each one. {viewport}
2. Point at Grouping to open its list. {groupButton}
3. Click Group. Use Ungroup or Ungroup All to break groups up again. {menu_group}
also: menu_ungroup, menu_ungroupall

Tip: Ctrl+G groups, Ctrl+Shift+G ungroups. Double-click a group to edit the parts inside it; Esc leaves it.

## Repeat a part in rows (Rectangular Pattern)
id: pattern-rectangular
keywords: pattern, rectangular, grid, array, rows, columns, repeat, copies, duplicate, multiple, many
Makes rows and columns of copies.

1. Point at Pattern to open its list. {patternButton}
2. Click Rectangular Pattern. {menu_rectpattern}
3. Click the solids to copy, then a straight edge for the direction. {stepBar}
4. Set the number of copies and the spacing, then press Enter. {patternHud}

## Repeat a part around a circle (Circular Pattern)
id: pattern-circular
keywords: circular, radial, around, circle, ring of, polar, spokes, evenly spaced around, rotate copies
Makes copies spread around an axis.

1. Point at Pattern to open its list. {patternButton}
2. Click Circular Pattern. {menu_circpattern}
3. Click the solids to copy, then a round edge, straight edge or cylinder for the axis. {stepBar}
4. Set the number of copies and the angle, then press Enter. {patternHud}

## Repeat a part along a path (Path Pattern)
id: pattern-path
keywords: path, along, follow, curve, edge, line of copies
Makes copies spread along an edge.

1. Point at Pattern to open its list. {patternButton}
2. Click Path Pattern. {menu_pathpattern}
3. Click the solids to copy, then the edge to follow. {stepBar}
4. Set the number of copies, then press Enter. {patternHud}

## Mirror a part
id: mirror
keywords: mirror, flip, reflect, symmetric, symmetry, opposite side, left and right
Makes a mirrored copy on the other side of a flat face.

1. Point at Pattern to open its list. {patternButton}
2. Click Mirror. {menu_mirror}
3. Click the solids to mirror, then a flat face as the mirror plane. {stepBar}
4. Press Enter to apply. {stepApply}

## Turn the workspace view: orbit, pan and zoom
id: view
keywords: view, camera, look, look at, from the top, top view, side view, front view, from above, rotate view, rotate the view, rotate workspace, rotate the workspace, rotate my workspace, turn the workspace, spin the view, orbit, orbit around, around the part, around my part, around a part, around it, pivot, centre of rotation, center of rotation, workspace, pan, zoom, closer, fit, home, angle, perspective, see, navigate
Changes how you look at the design: turn, slide or zoom the view. It never moves the parts. To turn a part itself, see "Move or rotate a part".

1. Right-drag to turn the view, middle-drag to slide it, and use the wheel to zoom. {viewport}
2. Click a face, edge or corner of the view cube to look from there. Point at the cube to show a house button, which returns to the start view. {viewCube}
3. To turn the view around a part, click the Orbit around the part button right under the cube. It keeps the selected part, or all parts when none is selected, in the middle of the view, and turning the view circles it. Select another part and the view moves to it. {nav_orbitpart}
4. These buttons make a left-drag pan, orbit or zoom instead of selecting. {nav_orbit}
5. Fit frames the selection, or everything. {nav_fit}
also: nav_pan, nav_zoom, viewCubeHome

Tip: press F to fit the view when nothing is selected.
Tip: sliding the view with a middle-drag turns Orbit around the part off, so you can move freely. Click the button again to centre the part.

## Change how the design looks (Visual style)
id: visual-style
keywords: style, look, outlines, edges, x-ray, xray, see through, transparent, shading, wireframe, display
Shows the design with materials, outlines, both, or see-through X-ray.

1. Point at the visual style button to open its list. {nav_style}
2. Click a style. {styleMenu}
also: style_materials_outlines, style_materials, style_outlines, style_xray

## Hide and show parts
id: hide-show
keywords: hide, show, show all, visible, invisible, unhide, hidden, disappear, vanish, missing part
Hides parts to get them out of the way. Hidden parts are still exported.

1. Click the part to select it. {viewport}
2. Point at the eye button to open its list. {nav_visibility}
3. Click Hide selected solids, or Show all to bring every part back. {visibilityMenu}
also: vis_hide, vis_showall

Tip: right-click a part and choose Hide Solid; right-click empty space and choose Show All.

## Grid, snapping and bed size
id: grid-snap
keywords: grid, snap, snapping, step, increment, precision, accuracy, exact, bed size, build plate, printer size, angle step, millimetre, mm
Drags step by the snap sizes; the grid is your printer's build plate.

1. The Snap box sets how far drags step, in millimetres. The Angle box sets how far turns step. {snapLinear}
2. Edit Grid sets the bed size to match your printer. {editGrid}
3. Grid snap makes drags step by the snap size. Implicit snap seats new shapes onto the face under the pointer. {nav_snapgrid}
4. The grid button shows or hides the grid lines. Snapping still works. {nav_grid}
also: snapAngular, nav_snappoints

## Undo and redo
id: undo
keywords: undo, redo, mistake, go back, revert, oops, wrong, ctrl z
Steps back through your changes, including changes the AI made.

1. Click Undo, or press Ctrl+Z. {undoAction}
2. Click Redo, or press Ctrl+Shift+Z. {redoAction}

## Save, open and start over
id: files
keywords: save, open, load, new, start over, file, save as, .cubra, quit, exit, close
Designs are saved as .cubra files.

1. Click the folder button. {folderButton}
2. Choose New, Open…, Save or Save As…. {file_save}
also: file_new, file_open, file_saveas, file_quit

Tip: Ctrl+S saves and Ctrl+O opens.

## Export for printing (STL or 3MF)
id: export
keywords: export, print, printing, slicer, stl, 3mf, cura, prusaslicer, bambu, orca, send to printer, download, output, file for printer
Saves the design as a mesh your slicer can open. All solids are fused into one watertight piece.

1. Click the folder button. {folderButton}
2. Choose Export STL… or Export 3MF…. {file_exportstl}
3. Pick a name and folder, then open the file in your slicer.
also: file_export3mf

Tip: 3MF keeps units and is the better choice for most modern slicers. Ctrl+E exports STL.

## Save a picture of the design
id: screenshot
keywords: screenshot, picture, image, png, capture, photo, share
Saves the 3D view as a PNG picture.

1. Click the camera button. {nav_screenshot}

## See and edit a part's history (Design panel)
id: design-panel
keywords: design panel, history, features, list, steps, dimensions, rename, name, edit size, change size later, operation, order
Lists each part's steps. You can rename parts and change the sizes of basic shapes.

1. Click the design panel button to show or hide it. {showDesignAction}
2. Select a part to see its steps and sizes. {designDock}

## Ask the AI assistant
id: ai
keywords: ai, assistant, chat, prompt, generate, describe, model, ollama, gpu, llm, ask, help, question, create with ai
Describe a part and the AI makes it, or ask how to do something.

1. Click the AI assistant button to show or hide the panel. {showAiAction}
2. Type a request such as "a 60 x 40 x 20 mm box with a 2 mm wall", or a question such as "how do I round an edge?", and press Enter. {aiDock}

Tip: "make a ..." starts a new part beside the others; "make it taller" changes the selected one. Ctrl+Z undoes anything the AI did.

## Measure a face
id: measure
keywords: measure, measurement, distance, size of face, area, how big, dimension, length, ruler
Shows the size of a face.

1. Click a part, then click the face to select it. {viewport}
2. Right-click the face and choose Measure.

Tip: the size of the whole design is shown in the status bar at the bottom.

## Design for 3D printing: walls and sizes
id: advice-walls
keywords: advice, tips, wall, thickness, thin, strong, strength, minimum, nozzle, fragile, sturdy, best practice, recommend
Advice: make walls at least 1.2 mm thick (three lines of a 0.4 mm nozzle), and 2 mm or more for parts that take force. Details smaller than about 0.8 mm may not print.

Tip: round or chamfer inside corners of parts that bend or take load: sharp inside corners crack first.

## Design for 3D printing: overhangs and supports
id: advice-overhangs
keywords: overhang, support, supports, bridge, bridging, droop, sag, angle, 45, upside down, orientation, print orientation, which way up
Advice: faces that lean out more than about 45 degrees from upright need supports. Chamfers print better than fillets on downward-facing edges. Turn the part so its largest flat face sits on the bed.

Tip: a hole sideways through a part prints rounder as a teardrop or diamond shape, which needs no support.

## Design for 3D printing: parts that fit together
id: advice-fit
keywords: fit, tolerance, clearance, gap, press fit, loose, tight, snug, assemble, assembly, lid, plug, peg, hole size, shrink
Advice: leave 0.2 mm of gap between parts that should press together and 0.4 mm for parts that should slide or turn. Printed holes come out slightly small, so make holes for bolts 0.2 to 0.4 mm wider.

Tip: print a small test piece with a few gap sizes before printing a large part.
