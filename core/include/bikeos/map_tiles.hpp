#pragma once

// Tiled city maps (BTIL): a whole city stored on flash as square tiles, each a
// self-contained BMAP v4 blob with its own origin, behind an index. The
// device decodes only the 3x3 tiles around the rider; see docs/map-data.md.

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <utility>
#include <vector>

#include "bikeos/geo.hpp"
#include "bikeos/map_data.hpp"

namespace bikeos {

struct TileKey {
    std::uint16_t column{};
    std::uint16_t row{};

    friend bool operator==(const TileKey&, const TileKey&) = default;
    friend bool operator<(const TileKey a, const TileKey b)
    {
        return a.row != b.row ? a.row < b.row : a.column < b.column;
    }
};

// A regular grid in degrees. Cell (column, row) covers
// [origin + row * tile_lat, origin + (row + 1) * tile_lat) north-south and the
// same east-west, so a tile's bounds follow from its key alone.
struct TileGrid {
    GeoPointE7 origin{};  // south-west corner of cell (0, 0)
    std::int32_t tile_lat_e7{};
    std::int32_t tile_lon_e7{};
    std::uint16_t columns{};
    std::uint16_t rows{};

    bool valid() const;
    // The cell containing `point`, clamped to the grid; nullopt for an empty grid.
    std::optional<TileKey> key_for(GeoPointE7 point) const;
    bool contains(GeoPointE7 point) const;
    GeoPointE7 tile_minimum(TileKey key) const;
    GeoPointE7 tile_maximum(TileKey key) const;  // exclusive edge, shared with the next tile
    GeoPointE7 tile_centre(TileKey key) const;
};

// Grid cells within `radius` cells of `centre` (a (2r+1)^2 block clipped to the grid), in row-major order.
std::vector<TileKey> tiles_around(const TileGrid& grid, TileKey centre, int radius = 1);

struct TileIndexEntry {
    TileKey key;
    std::uint32_t offset{};  // from the start of the blob
    std::uint32_t size{};
    std::uint32_t crc{};     // CRC-32 of the tile's BMAP bytes
};

// Read-only view of a BTIL blob, e.g. a flash partition mapped into memory.
// Opening checks the header and the index CRC only; each tile's CRC is
// checked when it is loaded, so boot does not read the whole city.
class TileArchive {
public:
    static constexpr std::size_t header_size = 40;
    static constexpr std::size_t index_entry_size = 16;

    bool open(std::span<const std::uint8_t> blob);
    bool is_open() const { return !blob_.empty(); }
    void close();

    const TileGrid& grid() const { return grid_; }
    std::size_t tile_count() const { return entries_.size(); }
    std::span<const TileIndexEntry> entries() const { return entries_; }
    const TileIndexEntry* find(TileKey key) const;
    // Verifies the tile CRC and decodes it; false (and `output` untouched) when absent or corrupt.
    bool load(TileKey key, MapDataset& output) const;
    // Size of the blob from its header, without opening it (0 if the header is not BTIL).
    static std::size_t declared_size(std::span<const std::uint8_t> header);

private:
    std::span<const std::uint8_t> blob_;
    TileGrid grid_;
    std::vector<TileIndexEntry> entries_;
};

// Writes a BTIL blob. Tiles must lie on `grid`, each tile at most once.
std::vector<std::uint8_t> encode_tile_archive(const TileGrid& grid,
                                              std::vector<std::pair<TileKey, MapDataset>> tiles);

// Clipping to a tile rectangle [minimum, maximum] in E7 degrees.
// A polyline that leaves and re-enters the tile becomes several pieces; the
// cut points lie exactly on the tile edge so neighbouring tiles meet.
std::vector<std::vector<GeoPointE7>> clip_polyline(std::span<const GeoPointE7> points, GeoPointE7 minimum,
                                                   GeoPointE7 maximum);
// Sutherland-Hodgman clip of a closed ring (first point not repeated); empty when nothing is left.
std::vector<GeoPointE7> clip_ring(std::span<const GeoPointE7> ring, GeoPointE7 minimum, GeoPointE7 maximum);

// Cuts a map into tiles on `grid`: roads and areas are clipped to each cell
// and cells with nothing left are omitted. The tool in tools/ does the same in
// Python for the city build; this one serves tests and small fixtures.
std::vector<std::pair<TileKey, MapDataset>> split_into_tiles(const MapDataset& map, const TileGrid& grid);

// The tiles around the rider, decoded and merged into one MapDataset for the
// renderer. Moving within the centre tile (plus a hysteresis margin) does
// nothing; crossing into another tile keeps the tiles still in range and
// decodes only the new ones.
class TileWindow {
public:
    // Margin before the window follows the rider into a neighbouring tile, so
    // riding along a tile edge does not rebuild the map every second. 500 m
    // tiles minus this margin still leave > 400 m of map on every side.
    static constexpr double hysteresis_m = 60.0;

    void set_archive(const TileArchive* archive);
    const TileArchive* archive() const { return archive_; }
    // Loads the tiles around `position`. Returns true when map() changed.
    bool update(GeoPointE7 position);
    void clear();

    const MapDataset& map() const { return map_; }
    MapDataset& map() { return map_; }
    std::optional<TileKey> centre() const { return centre_; }
    std::vector<TileKey> loaded_tiles() const;
    std::uint32_t tiles_decoded() const { return tiles_decoded_; }  // lifetime count, for logs and tests
    std::uint32_t tiles_failed() const { return tiles_failed_; }

private:
    struct Slice {
        TileKey key;
        std::size_t road_begin{};
        std::size_t road_count{};
        std::size_t area_begin{};
        std::size_t area_count{};
    };

    bool inside_with_margin(TileKey key, GeoPointE7 position) const;

    const TileArchive* archive_{};
    MapDataset map_;
    std::vector<Slice> slices_;
    std::optional<TileKey> centre_;
    std::uint32_t tiles_decoded_{};
    std::uint32_t tiles_failed_{};
};

}  // namespace bikeos
