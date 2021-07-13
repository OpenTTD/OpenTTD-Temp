add_subdirectory(settings)

add_files(
    airport_defaults.h
    airport_movement.h
    airporttile_ids.h
    airporttiles.h
    animcursors.h
    autorail.h
    bridge_land.h
    build_industry.h
    cargo_const.h
    clear_land.h
    control_codes.h
    elrail_data.h
    engines.h
    genland.h
    heightmap_colours.h
    industry_land.h
    landscape_sprite.h
    newgrf_debug_data.h
    object_land.h
    palette_convert.h
    palettes.h
    pricebase.h
    railtypes.h
    road_land.h
    roadtypes.h
    roadveh_movement.h
    sprites.h
    station_land.h
    strgen_tables.h
    string_colours.h
    town_land.h
    townname.h
    track_land.h
    train_cmd.h
    tree_land.h
    unicode.h
    water_land.h
)

add_files(
    opengl_shader.h
    CONDITION OPENGL_FOUND
)

add_files(
    hlsl_shader.h
    CONDITION D3D11_FOUND
)
