"""Generate smart-register.kicad_pro, sym-lib-table and fp-lib-table.

The project file carries net-class definitions/assignments and DRC severity
overrides (silk cosmetics + lib checks off — footprints are board-embedded,
there is no external library in CI).
"""

import json
import os

HERE = os.path.dirname(os.path.abspath(__file__))
PROJ = os.path.normpath(os.path.join(HERE, "..", "smart-register"))

USB_NETS = ["CC1", "CC2", "USB_DP", "USB_DN", "USB_DP_CONN", "USB_DN_CONN",
            "3V3", "SW_NODE", "BUCK_EN", "BUCK_FB"]

pro = {
    "board": {
        "3dviewports": [],
        "design_settings": {
            "defaults": {},
            "diff_pair_dimensions": [],
            "drc_exclusions": [],
            "rules": {
                "min_clearance": 0.1,
                "min_copper_edge_clearance": 0.3,
                "min_hole_clearance": 0.19,
                "min_hole_to_hole": 0.25,
                "min_microvia_diameter": 0.2,
                "min_microvia_drill": 0.1,
                "min_resolved_spokes": 1,
                "min_silk_clearance": 0.0,
                "min_text_height": 0.8,
                "min_text_thickness": 0.08,
                "min_through_hole_diameter": 0.3,
                "min_track_width": 0.2,
                "min_via_annular_width": 0.1,
                "min_via_diameter": 0.5,
                "solder_mask_to_copper_clearance": 0.0,
                "use_height_for_length_calcs": True,
            },
            "rule_severities": {
                "lib_footprint_issues": "ignore",
                "lib_footprint_mismatch": "ignore",
                "silk_over_copper": "ignore",
                "silk_overlap": "ignore",
                "silk_edge_clearance": "ignore",
                "silk_out_of_bounds": "ignore",
            },
        },
        "ipc2581": {},
        "layer_presets": [],
        "viewports": [],
    },
    "boards": [],
    "cvpcb": {"equivalence_files": []},
    "erc": {
        "erc_exclusions": [],
        "meta": {"version": 0},
        "pin_map": [],
        "rule_severities": {},
    },
    "libraries": {"pinned_footprint_libs": [], "pinned_symbol_libs": []},
    "meta": {"filename": "smart-register.kicad_pro", "version": 1},
    "net_settings": {
        "classes": [
            {
                "bus_width": 12,
                "clearance": 0.2,
                "diff_pair_gap": 0.25,
                "diff_pair_via_gap": 0.25,
                "diff_pair_width": 0.2,
                "line_style": 0,
                "microvia_diameter": 0.3,
                "microvia_drill": 0.1,
                "name": "Default",
                "pcb_color": "rgba(0, 0, 0, 0.000)",
                "schematic_color": "rgba(0, 0, 0, 0.000)",
                "track_width": 0.3,
                "via_diameter": 0.7,
                "via_drill": 0.35,
                "wire_width": 6,
            },
            {
                "bus_width": 12,
                "clearance": 0.13,
                "diff_pair_gap": 0.15,
                "diff_pair_via_gap": 0.25,
                "diff_pair_width": 0.2,
                "line_style": 0,
                "microvia_diameter": 0.3,
                "microvia_drill": 0.1,
                "name": "USB",
                "pcb_color": "rgba(0, 0, 0, 0.000)",
                "schematic_color": "rgba(0, 0, 0, 0.000)",
                "track_width": 0.2,
                "via_diameter": 0.6,
                "via_drill": 0.3,
                "wire_width": 6,
            },
        ],
        "meta": {"version": 3},
        "net_colors": None,
        "netclass_assignments": None,
        "netclass_patterns": [{"netclass": "USB", "pattern": n} for n in USB_NETS],
    },
    "pcbnew": {
        "last_paths": {},
        "page_layout_descr_file": "",
    },
    "schematic": {
        "annotate_start_num": 0,
        "bom_export_filename": "",
        "bom_fmt_presets": [],
        "bom_fmt_settings": {},
        "bom_presets": [],
        "bom_settings": {},
        "connection_grid_size": 50.0,
        "drawing": {
            "dashed_lines_dash_length_ratio": 12.0,
            "dashed_lines_gap_length_ratio": 3.0,
            "default_line_thickness": 6.0,
            "default_text_size": 50.0,
            "field_names": [],
            "intersheets_ref_own_page": False,
            "intersheets_ref_prefix": "",
            "intersheets_ref_short": False,
            "intersheets_ref_show": False,
            "intersheets_ref_suffix": "",
            "junction_size_choice": 3,
            "label_size_ratio": 0.375,
            "pin_symbol_size": 25.0,
            "text_offset_ratio": 0.15,
        },
        "legacy_lib_dir": "",
        "legacy_lib_list": [],
        "meta": {"version": 1},
        "net_format_name": "",
        "page_layout_descr_file": "",
        "plot_directory": "",
        "spice_current_sheet_as_root": False,
        "spice_external_command": 'spice "%I"',
        "spice_model_current_sheet_as_root": True,
        "spice_save_all_currents": False,
        "spice_save_all_dissipations": False,
        "spice_save_all_voltages": False,
        "subpart_first_id": 65,
        "subpart_id_separator": 0,
    },
    "sheets": [["e63e39d7-6ac0-4ffd-8aa3-1841a4541b55", "Root"]],
    "text_variables": {},
}

with open(os.path.join(PROJ, "smart-register.kicad_pro"), "w") as f:
    json.dump(pro, f, indent=2)

with open(os.path.join(PROJ, "sym-lib-table"), "w") as f:
    f.write("(sym_lib_table\n  (version 7)\n)\n")

with open(os.path.join(PROJ, "fp-lib-table"), "w") as f:
    f.write(
        '(fp_lib_table\n  (version 7)\n'
        '  (lib (name "SmartRegister")(type "KiCad")'
        '(uri "${KIPRJMOD}/lib/footprints/SmartRegister.pretty")'
        '(options "")(descr "project footprints"))\n)\n'
    )

print("wrote kicad_pro + lib tables")
