// Manhattan-island layout (port of world/layout.js — the grid core: avenues, per-street widths, shoreline, Central Park).
// Axes: +x = east, -z = north, y up, metres. Avenues run N-S (along z), streets run E-W (along x).
// The Greenwich Village / Financial District street maps (VMAP / FMAP) and Broadway's diagonal are not ported yet:
// the plain grid covers the whole island.
#pragma once
#include <vector>
#include <array>

namespace layout {

struct Grid {
  static constexpr float AV_ROAD = 22, AV_WALK = 5, ST_SP = 80, ST_ROAD = 10, ST_WALK = 4, PROM = 16;
  static constexpr float X_MIN = -790, X_MAX = 870, Z_MIN = -3480, Z_MAX = 3330;
  static constexpr float WATER_Y = -1.6f, CURB_H = 0.15f;
  static constexpr float AV_HALF = AV_ROAD / 2, ST_HALF = ST_ROAD / 2;
  static constexpr float PARK_X0 = -234, PARK_X1 = 234, PARK_Z0 = -2151, PARK_Z1 = -569;
};
// ground heights (ground.js GY)
constexpr float GY_ROAD = 0, GY_WALK = Grid::CURB_H, GY_GRASS = Grid::CURB_H + 0.02f, GY_WATER = Grid::WATER_Y;

extern const std::array<float, 7> AVENUES;
extern const char* AV_NAMES[7];
const std::vector<float>& streets();
float stHalf(int k);              // half roadway width of street k
float shoreW(float z);            // pushed (final) shoreline x, west / east
float shoreE(float z);
bool onLand(float x, float z);
// land x-range of the ORIGINAL shoreline over [za, zb] (the grid is laid out against it, layout.js landRange0)
std::array<float, 2> gridRange(float za, float zb);

enum Cell : int { C_WATER = 0, C_ROAD, C_WALK, C_PARK };
struct CellInfo { Cell type; int axis; float centre, half; }; // road: axis 0 = avenue (N-S), 1 = street (E-W), 2 = intersection
CellInfo classify(float x, float z);
float terrainHeight(float x, float z);
bool inParkCells(float x, float z);

// the band structure used to build the ground mesh: x boundaries (avenue edges) and z boundaries (street edges)
std::vector<float> xBands();
std::vector<float> zBands();
std::vector<float> shoreZ(); // shoreline resample points (tips + every street edge)

}  // namespace layout
