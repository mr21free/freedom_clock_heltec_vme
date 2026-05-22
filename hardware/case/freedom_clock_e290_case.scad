// ============================================================
//  Freedom Clock – Case v5
//  Heltec Vision Master E290  (87 × 37 × 12.5 mm board)
//
//  Board orientation:
//    Display face            → case FRONT  (Z = 0)
//    Board left / USB-C      → case LEFT   (X = 0 short end)
//    Buttons (RST/21/BOOT)   → BACK COVER – living-hinge islands
//                               (C-slot + post, no springs, no extra parts)
//
//  Physical orientation (as held / as printed):
//    USER'S TOP    = model Y = 0      = wall_b side  (USB-C entrance)
//    USER'S BOTTOM = model Y = case_w = button side  (living-hinge beams)
//    "Move down"   = increase Y in model coordinates
//
//  Logo: Freedom Clock icon (ring + 11:55→12:00 clock wedge),
//        raised 1 mm on front face, right bezel strip.
//
//  Back cover: snap-fit plug + 3 integrated living-hinge buttons.
//  Corner bosses: 4 × (6×4×1 mm) pads at inner corners, 2 mm mount hole.
// ============================================================

$fn = 64;

// ── Board ──────────────────────────────────────────────────
board_l = 87.0;   // 1 mm shorter than v4
board_w = 37.0;
board_h = 12.5;

// ── Display ────────────────────────────────────────────────
disp_w   = 68.0;
disp_h   = 30.0;
disp_x   = 7.0;                          // mm from board LEFT edge
disp_y   = (board_w - disp_h) / 2;      // = 3.5 mm (centred)
disp_from_right = board_l - disp_x - disp_w;  // = 12 mm

// Display window insets (add material around opening):
disp_win_ml  = 1.0;   // inset at left            (toward X = 0 / USB-C side)
disp_win_mr  = 1.25;  // inset at right           (toward X = case_l / logo side)
disp_win_mt  = 1.50;  // inset at user's BOTTOM   (toward Y = case_w / button side)
disp_win_mb  = 1.0;   // inset at user's TOP      (toward Y = 0 / USB-C side)

// ── Shell ──────────────────────────────────────────────────
wall       = 2.0;    // both short walls (X direction)
wall_b     = 2.25;   // user's TOP long wall  (Y=0,      USB-C side)
wall_bot   = 2.25;   // user's BOTTOM long wall (Y=case_w, button side) – 0.25 mm thicker inward
front_wall = 1.5;    // front face thickness (Z wall, display side)
tol   = 0.25;
R     = 3.5;
rear  = 1.4;

inner_l = board_l + 2*tol;            // 87.5
inner_w = board_w + 2*tol;            // 37.5
case_l  = inner_l + 2*wall;           // 91.5
case_w  = inner_w + 2*wall;           // 41.5
case_d  = front_wall + board_h + rear; // 15.4

// Front-face 45° bevel (all front Z-edges)
front_bevel = 2.0;

// ── USB-C (LEFT short end, X = 0 face) ─────────────────────
usb_cw  = 8.7;    // Y width — trimmed 0.25 mm from each long side
usb_ch  = 2.5;    // Z height of slot (effective 1 mm less from UP than original broken 3.5 mm)
usb_cr  = 1.5;    // corner radius → oval
usb_z0  = wall + 2.75;  // = 4.75 mm from front (moved 0.5 mm down toward display)
usb_bev = 1.5;    // chamfer at outer opening for easy cable insertion

// ── Cover panel ─────────────────────────────────────────────
cover_wall = 2.0;   // 2.0 mm — 1.0 mm text recess + 1.0 mm shell

// ── Post cross-section shape ─────────────────────────────────
// Override via: -D 'POST_SHAPE="d"' or -D 'POST_SHAPE="quadrant"'
POST_SHAPE = "d";        // "circle" | "d" | "quadrant"

// ── Text on cover back face ──────────────────────────────────
text_str   = "FREEDOM CLOCK";
text_size  = 6.0;
text_depth = 0.8;   // engraving depth on cover outer face

// ── Buttons – straight-cut cantilever beams in back cover ───
//    4 vertical slots + 1 horizontal slot form 3 independent beams.
//    Beams hinge at thin top strip; inner face pocket thins beam to
//    ~0.8 mm so even a 2 mm cover flexes with light press force.
btn_x        = [13.0, 20.0, 27.0];  // X from LEFT short edge
btn_y        = case_w - 4.6;        // Y position of tact switch centre (≈36.9 mm), 1.0 mm from plug wall inner face
btn_post_d   = 3.5;    // post diameter – unified with cap for full D-shape stability
btn_post_h   = 7.75;   // post height from thinned beam inner face
btn_cap_d    = 0.0;    // unified: cap same diameter as post
btn_cap_h    = 1.0;    // cap disc height (adds to total post length)
btn_icon_r   = 1.8;   // icon bounding radius (fits within 6 mm beam)
btn_icon_lw  = 0.55;  // icon line width
btn_icon_d   = 0.5;   // engraving depth (1.5 mm beam − 0.5 mm = 1.0 mm solid remaining)
btn_cut_w    = 1.0;    // slot width (as specified)
btn_beam_hw  = 3.0;    // half-beam width → 6 mm per beam (7 mm spacing − 1 mm cut)
btn_beam_h   = 8.0;    // beam length in Y (flex zone)
btn_strip_h  = 1.5;    // uncut hinge strip at top case edge (mm)
btn_pocket_d = 0.5;    // inner face pocket depth → cover_wall−0.5 = 1.5 mm beam thickness

// ── Corner mounting bosses (inside body) ──────────────────
corner_l = 6.0;    // length along X
corner_w = 4.0;    // width  along Y
corner_h = 1.0;    // height above inner floor
corner_d = 2.0;    // mounting hole diameter

// ── Corner board supports (inside cover plug) ────────────────
supp_l = 4.5;    // leg length along each wall
supp_h = 10.5;   // height from cover inner face into body

// ── Snap plug ──────────────────────────────────────────────
plug_tol  = 0.15;
plug_d    = 3.5;
plug_wall = 1.2;
tab_w     = 9.0;
tab_nib      = 1.4;
tab_slot     = 0.6;
tab_x_bot    = [0.50];         // body: bottom long wall indent – 1 at center
tab_x_top    = [0.25, 0.75];  // body: top long wall indents – 2
tab_y_fracs  = [0.25];         // body: each short wall indent – low Y
ctab_x_bot   = [0.25, 0.75];  // cover: bottom plug wall tabs – 2
ctab_x_top   = [0.50];         // cover: top plug wall tab – 1 at center
ctab_y_fracs = [0.75];         // cover: each short plug wall tab – high Y
indent_d  = 1.2;               // blind pocket depth — leaves ≥0.8 mm shell in 2 mm walls
indent_h  = 2.0;

// ── Logo ───────────────────────────────────────────────────
logo_inset  = 0.8;   // recess depth — deeper groove prints cleaner with 0.4 mm nozzle
logo_cx     = (wall + disp_x + tol + disp_w - disp_win_mr + case_l) / 2 - 0.5;
logo_cy     = case_w / 2;

// ─────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────

module rbox(l, w, d, rv = R) {
    hull()
        for (x = [rv, l-rv], y = [rv, w-rv])
            translate([x, y, 0]) cylinder(r=rv, h=d);
}

module outer_shell() {
    hull() {
        translate([front_bevel, front_bevel, 0])
            rbox(case_l-2*front_bevel, case_w-2*front_bevel, 0.01, R-front_bevel);
        translate([0, 0, front_bevel])
            rbox(case_l, case_w, case_d-front_bevel, R);
    }
}

module usb_oval(t = wall+0.2) {
    cr    = min(usb_cr, usb_ch/2, usb_cw/2);  // clamp so corners never overlap
    cr_o  = cr + usb_bev;
    hull() {
        // Inner face: regular oval
        for (y = [cr, usb_cw - cr])
            for (z = [cr, usb_ch - cr])
                translate([t - 0.01, y, z])
                    rotate([0, 90, 0]) cylinder(r = cr, h = 0.01);
        // Outer face: enlarged oval → 45° chamfer all around the opening
        for (y = [cr_o, usb_cw + 2*usb_bev - cr_o])
            for (z = [cr_o, usb_ch + 2*usb_bev - cr_o])
                translate([0, y - usb_bev, z - usb_bev])
                    rotate([0, 90, 0]) cylinder(r = cr_o, h = 0.01);
    }
}

// ── Freedom Clock logo ──────────────────────────────────────
module freedom_logo(h = logo_inset) {
    ro = 4.5;
    ri = ro * 0.794;
    gap_a = 240;
    gap_b = 270;
    linear_extrude(h) {
        union() {
            difference() {
                circle(r = ro, $fn = 64);
                circle(r = ri, $fn = 64);
            }
            polygon(concat(
                [[0, 0]],
                [for (a = [gap_a : 2 : gap_b + 0.1])
                    [ro * cos(a), ro * sin(a)]]
            ));
        }
    }
}

// ─────────────────────────────────────────────────────────
//  Snap-tab indents inside main body long walls
// ─────────────────────────────────────────────────────────

module snap_indents() {
    iz = case_d - cover_wall - plug_d + 1.75;
    // Bottom long wall – 2 snaps
    for (frac = tab_x_bot) {
        xc = wall + inner_l * frac;
        translate([xc - tab_w/2, wall_b - indent_d, iz])
            cube([tab_w, indent_d + 0.05, indent_h]);
    }
    // Top long wall – 1 snap at center
    for (frac = tab_x_top) {
        xc = wall + inner_l * frac;
        translate([xc - tab_w/2, case_w - wall_bot, iz])
            cube([tab_w, indent_d + 0.05, indent_h]);
    }
    // Short walls – 1 snap each (Y-direction indents)
    for (frac = tab_y_fracs) {
        yc = wall_b + (case_w - wall_b - wall) * frac;
        translate([wall - indent_d,   yc - tab_w/2, iz]) cube([indent_d + 0.05, tab_w, indent_h]);
        translate([wall + inner_l,    yc - tab_w/2, iz]) cube([indent_d + 0.05, tab_w, indent_h]);
    }
}

// ─────────────────────────────────────────────────────────
//  Corner mounting bosses (4 inner corners of body)
//  1 mm tall × 6 mm long × 4 mm wide, 2 mm through-hole.
// ─────────────────────────────────────────────────────────

module corner_bosses() {
    for (xi = [0, 1], yi = [0, 1]) {
        x0 = xi == 0 ? wall - tol : wall + inner_l + tol - corner_w;
        y0 = yi == 0 ? wall_b - tol : case_w - wall_bot + tol - corner_l;
        translate([x0, y0, front_wall])
            cube([corner_w, corner_l, corner_h]);
    }
}

// ─────────────────────────────────────────────────────────
//  Display window cut (shared geometry)
// ─────────────────────────────────────────────────────────

module display_window_cut() {
    ox = wall + disp_x - tol + disp_win_ml;
    oy = wall_b + disp_y - tol + disp_win_mb;
    dw = disp_w + 2*tol - disp_win_ml - disp_win_mr;
    dh = disp_h + 2*tol - disp_win_mt - disp_win_mb;
    bev = front_wall;   // 45° chamfer — same angle as front-face corner bevels
    translate([ox, oy, -0.05])
        hull() {
            translate([-bev, -bev, 0]) cube([dw+2*bev, dh+2*bev, 0.01]);
            cube([dw, dh, front_wall + 0.1]);
        }
}

// ─────────────────────────────────────────────────────────
//  Main Body (logo recessed 1.5 mm into front face)
// ─────────────────────────────────────────────────────────

module main_body() {
    difference() {
        outer_shell();
        translate([wall-tol, wall_b-tol, front_wall])
            cube([inner_l+2*tol, case_w-wall_b-wall_bot+2*tol, case_d]);
        display_window_cut();
        translate([-0.1, wall_b + (board_w - usb_cw)/2 + 0.00, usb_z0])  // centered; left 0.5 mm from previous (+Y toward user's BOTTOM)
            usb_oval();
        snap_indents();
        translate([logo_cx, logo_cy, -0.05])
            freedom_logo(h = logo_inset + 0.05);
    }
    corner_bosses();
}

// ─────────────────────────────────────────────────────────
//  Back Cover with living-hinge buttons
// ─────────────────────────────────────────────────────────

plug_l  = inner_l - 2*plug_tol;
plug_w  = case_w - wall_b - wall_bot - 2*plug_tol;   // asymmetric: both long walls thicker than short walls
plug_ox = (case_l - plug_l) / 2;
plug_oy = wall_b + plug_tol;                      // 0.25 mm closer to buttons vs symmetric

// Button face icons – engraved 0.3 mm into outer face of each beam.
// Left: square frame  |  Middle: X  |  Right: ring
module button_icons() {
    r  = btn_icon_r;
    lw = btn_icon_lw;
    d  = btn_icon_d;
    // Left – square frame
    translate([btn_x[0], btn_y, -0.05])
        linear_extrude(d + 0.05)
            difference() {
                square([r*2, r*2], center = true);
                square([r*2 - 2*lw, r*2 - 2*lw], center = true);
            }
    // Middle – X
    translate([btn_x[1], btn_y, -0.05])
        linear_extrude(d + 0.05)
            union() {
                rotate([0, 0,  45]) square([r*2, lw], center = true);
                rotate([0, 0, -45]) square([r*2, lw], center = true);
            }
    // Right – ring
    translate([btn_x[2], btn_y, -0.05])
        linear_extrude(d + 0.05)
            difference() {
                circle(r = r, $fn = 48);
                circle(r = r - lw, $fn = 48);
            }
}

// Straight-cut cantilever beam buttons.
// 1 horizontal slot frees beam bottoms; 4 vertical slots separate 3 beams.
// Inner face pocket thins beam zone to (cover_wall - btn_pocket_d) = 1.5 mm.
module button_straight_cuts() {
    bx0 = btn_x[0] - btn_beam_hw - btn_cut_w;  // left edge of leftmost vertical cut
    bx1 = btn_x[2] + btn_beam_hw + btn_cut_w;  // right edge of rightmost vertical cut
    by_hcut = case_w - btn_strip_h - btn_beam_h - btn_cut_w;  // bottom of horiz cut
    by_vbot = by_hcut + btn_cut_w;   // bottom of vertical cuts
    by_vtop = case_w - btn_strip_h;  // top of vertical cuts

    // Horizontal cut: frees beam bottoms from main panel body
    translate([bx0, by_hcut, -0.05])
        cube([bx1 - bx0, btn_cut_w, cover_wall + 0.1]);

    // 4 vertical cuts: left edge + 3 inter-beam separators
    for (bx = [bx0,
               btn_x[0] + btn_beam_hw,
               btn_x[1] + btn_beam_hw,
               btn_x[2] + btn_beam_hw])
        translate([bx, by_vbot, -0.05])
            cube([btn_cut_w, by_vtop - by_vbot, cover_wall + 0.1]);

    // Inner face pocket: thins beam to 1.5 mm all the way to the 0.8 mm hinge strip
    translate([bx0, by_hcut, cover_wall - btn_pocket_d])
        cube([bx1 - bx0, by_vtop - by_hcut + 0.05, btn_pocket_d + 0.05]);
}

// Right-triangle prisms at all 4 inner corners of the plug frame.
module _corner_supports() {
    for (xi = [0, 1]) {
        xc = xi == 0 ? plug_ox : plug_ox + plug_l;
        sx = xi == 0 ?  supp_l : -supp_l;
        // Bottom corners (no-button side)
        translate([xc, plug_oy, cover_wall])
            linear_extrude(supp_h)
                polygon([[0, 0], [sx, 0], [0,  supp_l]]);
        // Top corners (button side)
        translate([xc, plug_oy + plug_w, cover_wall])
            linear_extrude(supp_h)
                polygon([[0, 0], [sx, 0], [0, -supp_l]]);
    }
}

// Shorten button-side plug wall to 2.5 mm in button X area:
// 0.5 mm cut from base (cover sheet side) + 0.5 mm cut from tip.
module _btn_wall_height_cuts() {
    bx0 = btn_x[0] - btn_beam_hw - btn_cut_w - plug_ox;
    bx1 = btn_x[2] + btn_beam_hw + btn_cut_w - plug_ox;
    bw  = bx1 - bx0;
    yw  = plug_w - plug_wall - 0.01;  // Y start of button-side wall (plug-local)
    pw  = plug_wall + 0.02;
    // Cut 1.0 mm from base (button-pad/cover-sheet side) → wall height = 3.5 - 1.0 = 2.5 mm
    translate([bx0, yw, -0.01])
        cube([bw, pw, 1.01]);
}

// Post cross-section: circle | d-shape (flat toward +Y/plug wall) | quadrant (-X,-Y kept)
module _btn_post_body(h) {
    r = btn_post_d / 2;
    if (POST_SHAPE == "d") {
        // D-shape: flat face toward +Y (plug wall), rounded toward -Y
        intersection() {
            cylinder(r = r, h = h, $fn = 32);
            translate([-r, -r, 0]) cube([r*2, r, h]);
        }
    } else if (POST_SHAPE == "quadrant") {
        // Quadrant: flat faces toward +X and +Y, rounded corner toward -X,-Y
        intersection() {
            cylinder(r = r, h = h, $fn = 32);
            translate([-r, -r, 0]) cube([r, r, h]);
        }
    } else {
        cylinder(r = r, h = h, $fn = 32);
    }
}

module back_cover() {
    difference() {
        rbox(case_l, case_w, cover_wall);
        button_straight_cuts();
        button_icons();
        translate([case_l/2, case_w/2, -0.05])
            linear_extrude(text_depth + 0.05)
                mirror([1, 0, 0])
                    text(text_str, size = text_size,
                         font = "Liberation Sans:style=Bold",
                         halign = "center", valign = "center");
    }

    // Posts – D-shape body, full circle cap at tip
    for (bx = btn_x)
        translate([bx, btn_y, cover_wall - btn_pocket_d - 0.1]) {
            _btn_post_body(btn_post_h + 0.1);
            translate([0, 0, btn_post_h + 0.1])
                cylinder(d = btn_post_d, h = btn_cap_h, $fn = 32);
        }

    // Snap plug with slot cutouts for tabs
    translate([plug_ox, plug_oy, cover_wall]) {
        difference() {
            cube([plug_l, plug_w, plug_d]);
            translate([plug_wall, plug_wall, -0.1])
                cube([plug_l-2*plug_wall, plug_w-2*plug_wall, plug_d+0.2]);
            _plug_slots();
            _plug_slots_y();
            _btn_wall_height_cuts();
        }
    }

    // X-direction snap tabs (cover-specific positions)
    for (frac = ctab_x_bot) {
        xc = plug_ox + plug_l * frac;
        _snap_tab(xc, plug_oy, false);
    }
    for (frac = ctab_x_top) {
        xc = plug_ox + plug_l * frac;
        _snap_tab(xc, plug_oy + plug_w - plug_wall, true);
    }

    // Y-direction snap tabs (cover-specific positions)
    for (frac = ctab_y_fracs) {
        yc = plug_oy + plug_w * frac;
        _snap_tab_y(yc, plug_ox, false);
        _snap_tab_y(yc, plug_ox + plug_l - plug_wall, true);
    }

    _corner_supports();
}

module _plug_slots() {
    for (frac = ctab_x_bot) {
        xc = plug_l * frac;
        for (dx = [-(tab_w/2+tab_slot), tab_w/2])
            translate([xc+dx, -0.1, -0.1]) cube([tab_slot, plug_wall+0.2, plug_d+0.2]);
    }
    for (frac = ctab_x_top) {
        xc = plug_l * frac;
        for (dx = [-(tab_w/2+tab_slot), tab_w/2])
            translate([xc+dx, plug_w-plug_wall-0.1, -0.1]) cube([tab_slot, plug_wall+0.2, plug_d+0.2]);
    }
}

module _plug_slots_y() {
    for (frac = ctab_y_fracs) {
        yc = plug_w * frac;
        for (dy = [-(tab_w/2+tab_slot), tab_w/2]) {
            translate([-0.1,               yc+dy, -0.1]) cube([plug_wall+0.2, tab_slot, plug_d+0.2]);
            translate([plug_l-plug_wall-0.1, yc+dy, -0.1]) cube([plug_wall+0.2, tab_slot, plug_d+0.2]);
        }
    }
}

module _snap_tab(xc, y0, flip) {
    translate([xc - tab_w/2, y0, cover_wall])
    union() {
        cube([tab_w, plug_wall, plug_d]);
        hull() {
            translate([0, flip ? plug_wall + tab_nib : -tab_nib, plug_d - 1.5])
                cube([tab_w, 0.01, 0.01]);
            translate([0, flip ? plug_wall : 0, plug_d - 1.5])
                cube([tab_w, 0.01, 0.01]);
            translate([0, flip ? plug_wall : 0, plug_d])
                cube([tab_w, 0.01, 0.01]);
        }
    }
}

module _snap_tab_y(yc, x0, is_right) {
    translate([x0, yc - tab_w/2, cover_wall])
    union() {
        cube([plug_wall, tab_w, plug_d]);
        hull() {
            translate([is_right ? plug_wall + tab_nib : -tab_nib, 0, plug_d - 1.5])
                cube([0.01, tab_w, 0.01]);
            translate([is_right ? plug_wall : 0, 0, plug_d - 1.5])
                cube([0.01, tab_w, 0.01]);
            translate([is_right ? plug_wall : 0, 0, plug_d])
                cube([0.01, tab_w, 0.01]);
        }
    }
}

// ─────────────────────────────────────────────────────────
//  Render
// ─────────────────────────────────────────────────────────

PART = "both";   // -D 'PART="body"' | 'PART="cover"'

if      (PART == "body")  main_body();
else if (PART == "cover") back_cover();
else {
    color("OrangeRed", 0.92)  main_body();
    color("OrangeRed", 0.75)  translate([case_l + 10, 0, 0]) back_cover();
}
