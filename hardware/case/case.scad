// esp32-joystick2trainer JR bay module: 3D printed sleeve case.
//
// The PCB (22 x 62 x 1.6 mm) slides into the sleeve from the bay end; it is held by
// slots in the long walls. The bay end stays open: the 5-pin header protrudes there and
// the radio's bay closes it. The top end (USB-A) is closed except for the receptacle.
//
// Coordinates follow the PCB: x across the board (0..22), y along it (0 = USB-A end,
// 62 = bay end), z through the board (0 = board bottom face towards the radio,
// 1.6 = component side towards the pilot).
//
// Render:   openscad -o case_std.stl -D 'variant="std"' case.scad
//           openscad -o case_lite.stl -D 'variant="lite"' case.scad
//
// EVERY outer dimension below is a starting point: measure your radio's bay (width,
// thickness, depth, position of the 5-pin socket) before printing. See
// hardware/README.md, "Measurements to take on the radio".

variant = "std";          // "std" (standard JR bay) or "lite" (JR Lite / nano bay)

// ---- board ----------------------------------------------------------------
pcb_w = 22.0;
pcb_l = 62.0;
pcb_t = 1.6;
slot_clear = 0.25;        // per side, board-to-slot
slot_depth = 1.0;         // how far the board enters each side wall

// ---- clearances above and below the board ---------------------------------
top_clear = 7.6;          // USB-A receptacle is the tallest part on the component side
bottom_clear = 4.6;       // buck inductor (4.0 mm) + solder on the radio side

// ---- outer envelope per variant (TO BE MEASURED on the radio) ------------
outer_w = (variant == "std") ? 32.5 : 25.0;
outer_t = (variant == "std") ? 17.4 : 17.0;   // the USB-A receptacle (7.2 mm) sets the minimum
lip = 2.0;                // extra length at the USB-A end beyond the board
wall = 1.6;
end_wall = 1.6;

// ---- derived -------------------------------------------------------------
inner_w = pcb_w + 2 * slot_clear;                  // slot-to-slot width
side_wall = (outer_w - inner_w) / 2 + slot_depth;  // thick enough to carry the slot
case_l = pcb_l + end_wall + lip;
z_board = wall + bottom_clear;                      // board bottom face inside the case
assert(outer_t >= wall + bottom_clear + pcb_t + top_clear + wall,
       "outer_t too small for the board and its components");
eps = 0.01;

// ---- openings (board coordinates) ----------------------------------------
// USB-A receptacle in the end wall: 13.2 mm wide, opening at the top edge of the board
usba_x = [14.5 - 7.0, 14.5 + 7.0];
usba_z = [pcb_t, pcb_t + 7.2];
// USB-C on the left edge (x = 0), y 15.7..26.4
usbc_y = [15.0, 27.0];
usbc_z = [pcb_t - 0.3, pcb_t + 3.6];
// buttons on the component side: BOOT (SW2) and RESET (SW1)
buttons = [[3.4, 32.4], [15.0, 32.4]];
button_d = 3.6;
// LEDs: status (D5) and power (D6)
leds = [[3.85, 37.3], [3.85, 40.9]];
led_d = 2.0;
// AUX header J3 (GND/SBUS/CPPM) and I2C pins J4 on the component side
aux_x = [1.0, 8.2];  aux_y = [47.3, 56.3];
i2c_x = [18.6, 22.0]; i2c_y = [54.4, 60.8];
// solder jumpers stay covered; the bay header plastic body sits below the slot line

module board_frame() {
    // translate board coordinates to case coordinates
    translate([ (outer_w - pcb_w) / 2, end_wall + lip, z_board ]) children();
}

module outer() {
    hull() {
        for (x = [2, outer_w - 2], z = [2, outer_t - 2])
            translate([x, 0, z]) rotate([-90, 0, 0]) cylinder(r = 2, h = case_l, $fn = 32);
    }
}

module cavity() {
    board_frame() {
        // room above the board
        translate([-slot_clear, -lip, pcb_t - eps])
            cube([inner_w, pcb_l + lip + 1, top_clear + eps]);
        // room below the board
        translate([-slot_clear, -lip, -bottom_clear])
            cube([inner_w, pcb_l + lip + 1, bottom_clear + eps]);
        // the board itself plus the slots in the side walls
        translate([-slot_clear - slot_depth, -0.5, -slot_clear])
            cube([inner_w + 2 * slot_depth, pcb_l + 1.5, pcb_t + 2 * slot_clear]);
    }
}

module openings() {
    board_frame() {
        // USB-A through the end wall
        translate([usba_x[0], -lip - end_wall - 1, usba_z[0]])
            cube([usba_x[1] - usba_x[0], lip + end_wall + 2, usba_z[1] - usba_z[0]]);
        // USB-C through the left wall
        translate([-slot_clear - side_wall - 1, usbc_y[0], usbc_z[0]])
            cube([side_wall + 2, usbc_y[1] - usbc_y[0], usbc_z[1] - usbc_z[0]]);
        // buttons and LEDs through the component-side face
        for (b = buttons)
            translate([b[0], b[1], pcb_t]) cylinder(d = button_d, h = top_clear + wall + 1, $fn = 32);
        for (l = leds)
            translate([l[0], l[1], pcb_t]) cylinder(d = led_d, h = top_clear + wall + 1, $fn = 24);
        // AUX header and I2C pins
        translate([aux_x[0], aux_y[0], pcb_t]) cube([aux_x[1] - aux_x[0], aux_y[1] - aux_y[0], top_clear + wall + 1]);
        translate([i2c_x[0], i2c_y[0], pcb_t]) cube([i2c_x[1] - i2c_x[0], i2c_y[1] - i2c_y[0], top_clear + wall + 1]);
    }
}

module case() {
    difference() {
        outer();
        cavity();
        openings();
        // open bay end
        board_frame() translate([-slot_clear - side_wall - 1, pcb_l - 0.01, -bottom_clear - wall - 1])
            cube([inner_w + 2 * side_wall + 2, end_wall + 2, outer_t + 2]);
    }
}

case();
