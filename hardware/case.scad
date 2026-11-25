// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 UnluckyQuote

// ESP32-S3-DevKitC-1 slim snap-close case with USB-B plug tab
// Frame: x along the board length (USB-C end at x<0), y across, z up from the floor top.
// Print: box floor-down, lid plate-down (skirt up). Supports are needed for the USB-C holes.
part = "print";
$fn = 32; eps = 0.01;

board_l = 57;  board_w = 28;
pcb_t = 1.6;
board_total_h = 5.0;
top_h = board_total_h - pcb_t;
under_clear = 1.0;
support = "pads";
pad_size = 4;
board_clear = 0.25;
lid_gap = 0.4;
vent_n = 6; vent_pitch = 3.6; vent_w = 1.6;

usbc_w = 8.94 + 0.6;  usbc_h = 3.26 + 0.6;
usbc_pitch = 12.6;
usbc_cz = 1.63;
usb_over = 0.5;
pcb_end_gap = 0.2;
usb_wall = 1.2;

plug_flat_w = 7.5;
plug_flat_h = 8.0;
plug_chamfer = 1.75;
plug_scale = 0.99;
plug_w = plug_flat_w + 2*plug_chamfer;
plug_h = plug_flat_h + 2*plug_chamfer;
plug_rot = 0;
plug_clear = 0.2;
tab_margin = 5;

wall = 2; floor_t = 2; lid_t = 2.0; r_out = 3;
skirt_t = 1.2; skirt_h = 2.4; skirt_clear = 0.4; bump = 0.8; groove_d = 0.75;
band_half = 0.8; n = 4;
band_depth = 1.3;
band_z = skirt_h - band_depth;
peg_size = 2.5;
peg_press = 0.15;
snap_len = 12;
snap_pos = [0.28, 0.72];

vent_cx = pcb_end_gap + board_l - 11;
ant_len = 6.0;
ant_w = board_w - 0.5;
ant_clear = 0.4;
in_x = pcb_end_gap + board_l + board_clear + ant_len + ant_clear;
in_y = board_w + 2*board_clear;
in_h = under_clear + pcb_t + top_h + lid_gap;
plug_hole_w = (plug_w + 2*plug_clear) * plug_scale;
plug_hole_h = (plug_h + 2*plug_clear) * plug_scale;
ext_len = plug_hole_h + 2*tab_margin;

module rrect(x, y, r) { hull() for (i=[r, x-r], j=[r, y-r]) translate([i,j]) circle(r); }
module cavity2() rrect(in_x, in_y, 1);
module outer2() translate([-usb_wall, -wall]) rrect(in_x + usb_wall + wall + ext_len, in_y + 2*wall, r_out);

module snap_mask(extra = 0) {
  for (f=snap_pos) translate([f*in_x - (snap_len + 2*extra)/2, -wall - 1]) square([snap_len + 2*extra, in_y + 2*wall + 2]);
}

lid_fit = 0.15;
skirt_x0 = 8;
skirt_x1 = 4;
module skirt_span() translate([skirt_x0, -wall - 1]) square([in_x - skirt_x0 - skirt_x1, in_y + 2*wall + 2]);
module lid_outline2() {
  intersection() {
    outer2();
    translate([lid_fit, -wall - 1]) square([in_x - 2*lid_fit, in_y + 2*wall + 2]);
  }
}

module groove() {
  zc = in_h - band_depth;
  for (i=[0:n-1]) {
    d  = groove_d * (i+1)/n;
    hh = (band_half + 0.1) - d*(band_half-0.4)/groove_d;
    translate([0,0,zc - hh]) linear_extrude(2*hh) intersection() {
      difference() { offset(delta=d) cavity2(); cavity2(); }
      snap_mask(0.8);
    }
  }
}

module usbc_hole() {
  zc = under_clear + pcb_t + usbc_cz;
  for (s=[-1,1]) translate([-usb_wall-1, in_y/2 + s*usbc_pitch/2, zc])
    rotate([90,0,90]) linear_extrude(usb_wall + 1 + pcb_end_gap + 0.5)
      hull() for (k=[-1,1]) translate([k*(usbc_w/2 - usbc_h/2), 0]) circle(d=usbc_h);
}

module plug_hole2d(clear = plug_clear, c = plug_chamfer) {
  W = plug_flat_w + 2*c + 2*clear; H = plug_flat_h + 2*c + 2*clear;
  rotate(plug_rot) polygon([[-W/2+c,-H/2],[W/2-c,-H/2],[W/2,-H/2+c],[W/2,H/2-c],[W/2-c,H/2],[-W/2+c,H/2],[-W/2,H/2-c],[-W/2,-H/2+c]]);
}

module box() {
  difference() {
    union() {
      translate([0,0,-floor_t]) linear_extrude(in_h + floor_t) outer2();
      translate([0,0,-floor_t]) linear_extrude(in_h + floor_t + lid_t) intersection() {
        outer2();
        union() {
          translate([-usb_wall - 1, -wall - 1]) square([usb_wall + 1, in_y + 2*wall + 2]);
          translate([in_x, -wall - 1]) square([wall + ext_len + 1, in_y + 2*wall + 2]);
        }
      }
    }
    translate([0,0,-eps]) linear_extrude(in_h + 1) cavity2();
    groove();
    usbc_hole();
    translate([in_x + wall + ext_len/2, in_y/2, -floor_t - 1]) linear_extrude(floor_t + in_h + lid_t + 2) scale(plug_scale) plug_hole2d();
  }
  if (support == "pads")
    for (x0=[pcb_end_gap, pcb_end_gap + board_l - pad_size], y0=[board_clear, board_clear + board_w - pad_size])
      translate([x0, y0, -eps]) cube([pad_size, pad_size, under_clear + eps]);
  if (support == "rails")
    for (y0=[board_clear, board_clear + board_w - 2])
      translate([pcb_end_gap, y0, -eps]) cube([board_l, 2, under_clear + eps]);
}

module lid() {
  linear_extrude(lid_t) difference() {
    lid_outline2();
    for (i=[0:vent_n-1]) translate([vent_cx + (i - (vent_n-1)/2)*vent_pitch - vent_w/2, in_y/2 - (in_y-8)/2]) square([vent_w, in_y-8]);
  }
  for (x0=[pcb_end_gap + 0.4, pcb_end_gap + board_l - 0.4 - peg_size], y0=[board_clear + 0.4, board_clear + board_w - 0.4 - peg_size])
    translate([x0, y0, lid_t - eps]) cube([peg_size, peg_size, top_h + lid_gap + peg_press + eps]);
  top = lid_t + skirt_h;
  translate([0,0,lid_t-eps]) linear_extrude(skirt_h+eps) intersection() {
    difference() {
      offset(delta=-skirt_clear) cavity2();
      offset(delta=-skirt_clear-skirt_t) cavity2();
    }
    skirt_span();
  }
  zc = top - band_z;
  for (i=[0:n-1]) {
    p  = bump*(i+1)/n;
    hh = band_half - p*(band_half-0.4)/bump;
    translate([0,0,zc - hh]) linear_extrude(2*hh) intersection() {
      difference() { offset(delta=-skirt_clear+p) cavity2(); offset(delta=-skirt_clear-skirt_t) cavity2(); }
      snap_mask();
    }
  }
}

module ghost() {
  color("green", 0.35) translate([pcb_end_gap, board_clear, under_clear]) cube([board_l, board_w, pcb_t]);
  color("silver", 0.5) for (s=[-1,1])
    translate([pcb_end_gap - usb_over, in_y/2 + s*usbc_pitch/2 - 8.94/2, under_clear + pcb_t]) cube([7, 8.94, 3.26]);
  color("gold", 0.6) translate([pcb_end_gap + board_l, in_y/2 - ant_w/2, under_clear]) cube([ant_len, ant_w, 0.7]);
}

if (part == "box") box();
else if (part == "lid") lid();
else if (part == "print") { box(); translate([0, in_y + 2*wall + 12, -floor_t]) lid(); }
else if (part == "closed") { box(); translate([0, in_y, in_h + lid_t]) rotate([180,0,0]) lid(); }
else if (part == "closed_lid") translate([0, in_y, in_h + lid_t]) rotate([180,0,0]) lid();
else { box(); ghost(); color("orange", 0.8) translate([0, in_y, in_h + lid_t]) rotate([180,0,0]) lid(); }
