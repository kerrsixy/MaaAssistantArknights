"""Expose the native registered-grid mover without weakening legacy pixel gates."""
import re


def relocation_options(data, name, source):
    def cell(value):
        return (isinstance(value, list) and len(value) == 2
                and all(type(x) is int for x in value)
                and 0 <= value[0] <= 10 and 0 <= value[1] <= 4)

    origin, target = data.get("origin"), data.get("destination")
    units = data.get("placement_units", [])
    if (not re.fullmatch(r"m\d{2}", data.get("map_id", "")) or not cell(origin)
            or not cell(target) or origin == target or not isinstance(units, list)
            or not 1 <= len(units) <= 8):
        raise ValueError("Relocation requires map, distinct grid cells and verified field entities")
    names, points = set(), set()
    for unit in units:
        point = (unit.get("x"), unit.get("y"))
        if (not unit.get("name") or unit["name"] in names or point in points
                or not all(type(x) is int for x in point)
                or not 180 <= point[0] < 1000 or not 140 <= point[1] < 510
                or unit.get("field_pose_verified") is not True
                or unit.get("coordinate_frame") != "collapsed"):
            raise ValueError("Relocation field snapshot is incomplete or ambiguous")
        names.add(unit["name"])
        points.add(point)
    matches = [u for u in units if u["name"] == name and [u["x"], u["y"]] == source]
    if len(matches) != 1:
        raise ValueError("Relocation source does not match a unique verified field entity")
    result = {"relocate_field": True, "reorient_col": target[0], "reorient_row": target[1],
              "origin_col": origin[0], "origin_row": origin[1], "expected_map_id": data["map_id"],
              "placement_units": units}
    if "registered_grid" in data:
        prior = data["registered_grid"]
        if (not isinstance(prior, dict) or prior.get("map_id") != data["map_id"]
                or not isinstance(prior.get("reference_file"), str)
                or not isinstance(prior.get("projection"), list) or len(prior["projection"]) != 9
                or any(type(x) not in (float, int) for x in prior["projection"])):
            raise ValueError("Registered reference requires a same-map image and full projection")
        result["registered_grid"] = prior
    return result


def relocation_verified(what, receipt):
    return (what == "SPAFieldRelocationVerified"
            and all(receipt.get(k) is True for k in ["relocation_applied", "direction_input_committed",
                   "population_preserved", "funds_preserved", "source_identity_and_field_control_verified"])
            and type(receipt.get("field_coordinates_registered")) is int
            and receipt["field_coordinates_registered"] > 0)
