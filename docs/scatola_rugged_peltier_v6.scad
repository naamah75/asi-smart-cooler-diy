// =======================================================
// SCATOLA RUGGED PELTIER - V6
// 100 x 50 x 50 mm
// Corpo + coperchio separati
// OpenSCAD
//
// Modifica richiesta:
// - sul lato del foro jack NON c'e' il rettangolo ornamentale centrale
// - restano solo due nervature laterali decorative
// - torrette viti estese fino agli anelli superiori: niente aria tra i pezzi
// =======================================================

$fn = 64;

// ---------- PARAMETRI PRINCIPALI ----------
box_x = 100;
box_y = 50;
box_z = 50;

wall = 2.5;
corner_r = 6;

lid_h = 3;
lip_h = 2;

screw_offset = 7;
screw_d = 3.2;
screw_head_d = 7.2;
boss_d = 8.5;

jack_d = 8.2;       // foro jack alimentazione
cable_d = 3.5;      // fori passaggio cavi Peltier/NTC

rib_w = 3.5;
rib_depth = 2.2;
rib_h = 28;

// Le torrette ora arrivano fino agli anelli superiori.
boss_h = box_z;

// ---------- UTILITY ----------
module rounded_rect(size=[10,10,5], r=2) {
    hull() {
        for (x=[r, size[0]-r])
        for (y=[r, size[1]-r])
            translate([x,y,0])
                cylinder(h=size[2], r=r);
    }
}

module rounded_frame(size=[100,50,3], r=5, t=4) {
    difference() {
        rounded_rect(size, r);
        translate([t,t,-0.1])
            rounded_rect([size[0]-2*t, size[1]-2*t, size[2]+0.2], max(r-t,1));
    }
}

module screw_positions() {
    for (x=[screw_offset, box_x-screw_offset])
    for (y=[screw_offset, box_y-screw_offset])
        translate([x,y,0]) children();
}

// Nervatura verticale sul lato corto X=0 oppure X=box_x
module short_side_rib(xpos=0, ypos=10) {
    translate([xpos, ypos, 12])
        cube([rib_depth, rib_w, rib_h]);
}

// ---------- CORPO ----------
module rugged_body_beauty() {
    union() {
        difference() {
            union() {
                // corpo base stondato
                rounded_rect([box_x, box_y, box_z], corner_r);

                // bumper angolari rinforzati
                screw_positions()
                    cylinder(h=box_z, d=15);

                // fascia superiore rugged
                translate([0,0,box_z-6])
                    rounded_frame([box_x,box_y,6], corner_r, 4);

                // fascia centrale decorativa leggera
                translate([0,0,25])
                    rounded_frame([box_x,box_y,2], corner_r, 3);
            }

            // svuotamento interno
            translate([wall,wall,wall])
                rounded_rect(
                    [box_x-2*wall, box_y-2*wall, box_z+1],
                    max(corner_r-wall,1)
                );

            // foro jack tondo, lato alimentazione / frontale
            translate([-1,box_y/2,box_z/2])
                rotate([0,90,0])
                    cylinder(d=jack_d,h=wall+8);

            // fori cavi Peltier + NTC, lato opposto
            for (dy=[-8,8])
                translate([box_x-wall-2,box_y/2+dy,box_z/2])
                    rotate([0,90,0])
                        cylinder(d=cable_d,h=wall+8);
        }

        // torrette viti interne - collegate agli anelli superiori
        screw_positions()
            difference() {
                cylinder(h=boss_h,d=boss_d);
                translate([0,0,-1])
                    cylinder(h=boss_h+2,d=screw_d);
            }

        // anelli superiori attorno alle viti, ora fusi con le torrette
        screw_positions()
            translate([0,0,box_z-3])
                difference() {
                    cylinder(h=3.2,d=13.5);
                    translate([0,0,-0.2])
                        cylinder(h=3.8,d=screw_head_d);
                }

        // nervature laterali lunghe
        for (x=[18:12:82]) {
            translate([x,-rib_depth,12])
                cube([rib_w,rib_depth,rib_h]);

            translate([x,box_y,12])
                cube([rib_w,rib_depth,rib_h]);
        }

        // lato alimentazione: SOLO due nervature laterali.
        // Il rettangolo/nervatura centrale e' stato rimosso per lasciare pulito il foro jack.
        short_side_rib(-rib_depth, 11);
        short_side_rib(-rib_depth, box_y-11-rib_w);

        // lato cavi: mantengo tre nervature decorative
        for (y=[13:12:37])
            short_side_rib(box_x, y);

        // ghiera jack frontale
        translate([-2.3,box_y/2,box_z/2])
            rotate([0,90,0])
                difference() {
                    cylinder(h=2.5,d=16);
                    translate([0,0,-0.2])
                        cylinder(h=3,d=jack_d);
                }

        // piastra estetica lato cavi
        translate([box_x,box_y/2-15,box_z/2-10])
            difference() {
                cube([2.5,30,20]);

                for (dy=[-8,8])
                    translate([-0.2,15+dy,10])
                        rotate([0,90,0])
                            cylinder(d=cable_d+1.2,h=3);
            }

        // bordino inferiore leggero
        translate([0,0,0])
            rounded_frame([box_x,box_y,2], corner_r, 4);
    }
}

// ---------- COPERCHIO ----------
module rugged_lid_beauty() {
    union() {
        difference() {
            union() {
                // base coperchio
                rounded_rect([box_x,box_y,lid_h], corner_r);

                // cornice esterna rialzata
                translate([0,0,lid_h])
                    rounded_frame([box_x,box_y,3], corner_r, 5);

                // cornice interna mesh
                translate([11,8,lid_h+3])
                    rounded_frame([box_x-22,box_y-16,2], 3.5, 3);

                // pad viti superiori
                screw_positions()
                    translate([0,0,lid_h])
                        cylinder(h=3.2,d=14);
            }

            // fori viti
            screw_positions()
                translate([0,0,-1])
                    cylinder(h=lid_h+8,d=screw_d);

            // sede testa vite
            screw_positions()
                translate([0,0,lid_h+1.6])
                    cylinder(h=3,d=screw_head_d);

            // honeycomb / mesh esagonale
            for (x=[18:7:82])
            for (y=[12:6.2:38])
                translate([x + ((floor(y/6.2)%2)*3.5), y, -1])
                    cylinder(h=lid_h+9,d=4.2,$fn=6);
        }

        // labbro inferiore di centraggio
        translate([wall+0.35,wall+0.35,-lip_h])
            rounded_frame(
                [box_x-2*(wall+0.35), box_y-2*(wall+0.35), lip_h],
                max(corner_r-wall,1),
                2
            );
    }
}

// ---------- RENDER / EXPORT ----------

// Solo corpo per esportare STL:
rugged_body_beauty();

// Anteprima con coperchio sopra:
// translate([0,0,62]) rugged_lid_beauty();

// Solo coperchio per esportare STL:
// rugged_lid_beauty();
