// Line sensor bracket - LAFVIN 4WD mecanum chassis + QYF-750 8-channel line board
//
// Hangs the line sensor board under the front of the chassis, centred on the
// robot, sensor faces LINE_GAP above the floor. Nothing sits on TOP of the
// chassis (the sonar bracket covers that area with 0.1 mm to spare):
//   - a thin flange sits UNDER the chassis and is clamped by the sonar
//     bracket's own two small (M1.5 / M1.6) screws and nuts (no new screws);
//   - the front-most hole pair takes two screws from the top (heads on the
//     chassis at the very front edge) with nuts underneath: a 5 x 5 window
//     through the wall's front face lets each nut slide in under the flange;
//   - four screws in all (two front, two sonar) locate it; no pegs, which
//     would only snap off;
//   - a wall drops straight down at the front edge to a shelf that runs
//     BACK under the chassis (same direction as the flange: a C shape); the
//     board screws to the shelf from below with two M2 x 6 kit screws
//     threading into the plastic (no nuts), header edge against the wall;
//   - a window in the shelf over the header pins lets the wires go straight
//     up behind the wall and back under the chassis to the wire slot behind
//     the sonar row.
//
// Frame: x = right (0 = robot centreline), y = BACKWARD from the chassis front
// edge at the front hole pair, z = up from the floor. All sizes in mm.
// Print FACE DOWN: the wall's front face on the bed, flange and shelf standing
// up. No supports.
// Measured 2026-09-25; the lines marked CHECK are estimates - measure first.

// ---- chassis ----
chassis_underside_z   = 41;     // top of chassis 46 above floor, 5 thick
chassis_thickness     = 5;
front_pair_spacing    = 27;     // front hole rectangle, left-right
front_pair_y          = 3.0;    // CHECK: 1.5 mm hole-edge to chassis edge -> centre 3.0 behind the edge
second_pair_y         = 13.0;   // 10 mm behind the front pair (not used)
sonar_row_y           = 22.0;   // CHECK: estimated from the photo (front pair + ~19)
sonar_screw_spacing   = 20.25;  // CHECK: outer pair, M1.6-ish screws; calipers gave 22.5 outside / 18 inside
sonar_inner_spacing   = 12.5;   // inner pair, M1.5/M1.6-size (not used)
motor_gap             = 33;     // between the two front motors at the sonar row
chassis_hole_d        = 3.0;

// ---- line board (QYF-750) ----
line_gap              = 2.5;    // floor to sensor faces (1-3 works; 2-3 chosen)
board_sensor_to_back  = 3.0;    // sensor face to the back of the PCB
pcb_width             = 66;
pcb_depth             = 11;
pcb_hole_spacing      = 36;
pcb_hole_from_pin_edge = 4.0;   // CHECK: hole centres to the header-side PCB edge
pin_row_from_pin_edge = 1.5;    // header pins to that edge
pin_window_width      = 29;     // 10 pins at 2.54 + Dupont housings
board_gap_to_wall     = 1.0;    // gap between the PCB's header edge and the wall

// ---- part ----
flange_thickness      = 2.0;    // 4.3 of screw below the chassis: 2.0 + an M1.6 nut (1.3) fits
wall_thickness        = 2.5;
shelf_thickness       = 2.5;
wall_half_width       = 22;
flange_front_half     = 20;     // wide part at the front (ahead of the motors)
flange_front_depth    = 9;
flange_rear_half      = 15;     // must pass between the motors (motor_gap / 2 = 16.5)
front_screw_clear_d   = 3.2;    // front-most pair: clearance for M3 (fine for M2 too)
front_screw_depth     = 13;     // screw tail clearance below the flange top: M2/M3 x 10-12 fit
nut_window            = 5;      // 5 x 5 window for the nut, just under the flange
screw_slot_d          = 2.2;    // clearance for the sonar screws (M1.5 / M1.6)
screw_slot_play       = 3.0;    // extra length front-back, in case sonar_row_y is off
m2_pilot_d            = 1.7;    // M2 screw threads into the plastic
gusset_thickness      = 3;
$fn = 40;

// ---- derived ----
board_back_z = line_gap + board_sensor_to_back;          // shelf underside
shelf_top_z  = board_back_z + shelf_thickness;
flange_bottom_z = chassis_underside_z - flange_thickness;
pcb_pin_edge_y    = wall_thickness + board_gap_to_wall;  // header edge, against the wall
pcb_sensor_edge_y = pcb_pin_edge_y + pcb_depth;
pcb_hole_y   = pcb_pin_edge_y + pcb_hole_from_pin_edge;
pin_row_y    = pcb_pin_edge_y + pin_row_from_pin_edge;
flange_rear_y = sonar_row_y + 4;

assert(flange_rear_half * 2 < motor_gap, "flange too wide to pass between the motors");

module flange() {
  difference() {
    union() {
      translate([-flange_front_half, 0, flange_bottom_z])
        cube([flange_front_half * 2, flange_front_depth, flange_thickness]);
      translate([-flange_rear_half, 0, flange_bottom_z])
        cube([flange_rear_half * 2, flange_rear_y, flange_thickness]);
    }
    // sonar screws: slots, so a little error in sonar_row_y still fits
    for (side = [-1, 1])
      hull() for (dy = [-screw_slot_play / 2, screw_slot_play / 2])
        translate([side * sonar_screw_spacing / 2, sonar_row_y + dy, flange_bottom_z - 1])
          cylinder(d = screw_slot_d, h = flange_thickness + 2);
  }
}

module wall() {
  translate([-wall_half_width, 0, board_back_z])
    cube([wall_half_width * 2, wall_thickness, flange_bottom_z - board_back_z + 0.01]);
  // gussets behind the wall, under the wide front part of the flange
  for (x = [-(flange_front_half - gusset_thickness / 2), flange_front_half - gusset_thickness / 2])
    translate([x - gusset_thickness / 2, wall_thickness, flange_bottom_z])
      rotate([0, 90, 0]) linear_extrude(gusset_thickness)
        polygon([[0, 0], [0, flange_front_depth - wall_thickness], [flange_bottom_z - shelf_top_z - 4, 0]]);
}

module shelf() {
  difference() {
    translate([-wall_half_width, 0, board_back_z])
      cube([wall_half_width * 2, pcb_sensor_edge_y, shelf_thickness]);
    // M2 pilot holes: screws come up from below through the board
    for (side = [-1, 1])
      translate([side * pcb_hole_spacing / 2, pcb_hole_y, board_back_z - 1])
        cylinder(d = m2_pilot_d, h = shelf_thickness + 2);
    // window over the header pins: the wires go straight up behind the wall
    translate([-pin_window_width / 2, wall_thickness, board_back_z - 1])
      cube([pin_window_width, pin_row_y + 1.8 - wall_thickness, shelf_thickness + 2]);
  }
}

module line_board_ghost() {
  // for the preview only: the PCB and its sensors
  %translate([-pcb_width / 2, pcb_pin_edge_y, board_back_z - 1.5]) cube([pcb_width, pcb_depth, 1.5]);
  %for (i = [0:7]) translate([-28 + i * 8 - 1.25, pcb_sensor_edge_y - 4, line_gap]) cube([2.5, 3, 1.5]);
}

// Screw hole through the flange (and the back of the wall below it, for the
// screw's tail), plus the nut window: 5 x 5 through the wall's front face,
// right under the flange.
module front_screw_holes() {
  for (side = [-1, 1]) {
    translate([side * front_pair_spacing / 2, front_pair_y, chassis_underside_z - front_screw_depth])
      cylinder(d = front_screw_clear_d, h = front_screw_depth + 1);
    translate([side * front_pair_spacing / 2 - nut_window / 2, -1, flange_bottom_z - nut_window])
      cube([nut_window, front_pair_y + nut_window / 2 + 1, nut_window]);
  }
}

module bracket() {
  difference() {
    union() {
      flange();
      wall();
      shelf();
    }
    front_screw_holes();
  }
}

// "bracket" = the whole part. "fit_test" = only the flange, flat: a few
// minutes to print, to check the holes against the chassis
// before printing the real thing.
part = "bracket";

if (part == "fit_test") {
  translate([0, 0, -flange_bottom_z]) difference() { flange(); front_screw_holes(); }
} else {
  bracket();
  line_board_ghost();
}
