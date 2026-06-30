// E-Ink Voice Agent Case Design
// Based on Waveshare ESP32-C6-ePaper-1.54 (200x200 display)
// Parametric design - adjust dimensions as needed

// == Board Dimensions (mm) ==
// ESP32-C6-ePaper-1.54: 68mm x 35mm x 1.6mm (L x W x H)
// Display: 24mm x 24mm visible area (200x200 pixels)
// USB-C on left edge, buttons on right edge below display

board_length = 68;
board_width = 35;
board_thickness = 1.6;

// == Display Window ==
// Display sits on right side, rotated portrait
display_size = [24, 24];
display_x_offset = 35;  // From left edge
display_y_offset = 5;   // From top edge
display_bezel = 1.5;

// == Button Cutouts ==
// 4 tactile buttons below display
button_diameter = 6;
button_rows = 2;
button_cols = 2;
button_spacing_x = 14;
button_spacing_y = 10;
button_start_x = 42;
button_start_y = 12;

// == Audio Components ==
// Speaker: MAX98357A + 18mm speaker on left side
speaker_diameter = 18;
speaker_offset_x = 12;
speaker_offset_y = 25;

// Microphone: INMP441 on front, top-left near USB-C
mic_grille_diameter = 8;
mic_offset_x = 8;
mic_offset_y = 12;

// == Wall Thickness ==
wall = 1.5;
lip = 1;

// == Parts ==

// Main case body
module case_body() {
    difference() {
        // Outer shell
        cube([board_length + 2*wall, board_width + 2*wall, 20]);
        
        // Inner cavity
        translate([wall, wall, 0])
            cube([board_length, board_width, 22]);
        
        // Display window (portrait orientation)
        translate([display_x_offset + wall - display_bezel,
                  display_y_offset + wall - display_bezel,
                  -1])
            cube([display_size[0] + 2*display_bezel, display_size[1] + 2*display_bezel, 3]);
        
        // Button cutouts (2x2 grid)
        for(row = [0 : button_rows - 1]) {
            for(col = [0 : button_cols - 1]) {
                translate([button_start_x + wall + col * button_spacing_x - button_diameter/2,
                          button_start_y + wall + row * button_spacing_y - button_diameter/2,
                          -1])
                    cylinder(d=button_diameter, h=3, $fn=16);
            }
        }
        
        // Speaker cutout
        translate([speaker_offset_x + wall - speaker_diameter/2,
                  speaker_offset_y + wall - speaker_diameter/2, -1])
            cylinder(d=speaker_diameter, h=3, $fn=32);
        
        // Microphone grille
        translate([mic_offset_x + wall - mic_grille_diameter/2,
                  mic_offset_y + wall - mic_grille_diameter/2, -1])
            cylinder(d=mic_grille_diameter, h=3, $fn=32);
    }
}

// Case lid
module case_lid() {
    difference() {
        // Outer shell
        cube([board_length + 2*wall - 2*lip, board_width + 2*wall - 2*lip, 15]);
        
        // Inner cavity
        translate([lip, lip, 0])
            cube([board_length + 2*wall - 2*lip, board_width + 2*wall - 2*lip, 16]);
    }
}

// Battery compartment (optional, for 1000mAh LiPo)
module battery_compartment() {
    translate([board_length/2, -wall - 10, 0])
        difference() {
            cube([30, 12, 8]);
            translate([5, 4, -1]) cylinder(d=4, h=10, $fn=16); // Wire hole
        }
}

// USB-C cutout
module usb_cutout() {
    translate([board_length - 2, board_width/2, -1])
        cube([4, 12, 3]);
}

// == Render ==
case_body();