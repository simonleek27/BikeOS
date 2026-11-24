#include "bikeos/map_tiles.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <type_traits>

namespace bikeos {
namespace {

constexpr std::array<std::uint8_t, 4> kMagic{'B', 'T', 'I', 'L'};
constexpr std::uint16_t kVersion = 1;
constexpr double kMetresPerE7 = 6371008.8 * 3.14159265358979323846 / 180.0 * 1.0e-7;

template <typename T>
void put_le(std::vector<std::uint8_t>& output, T value)
{
    using Unsigned = std::make_unsigned_t<T>;
    const auto bits = static_cast<Unsigned>(value);
    for (std::size_t i = 0; i < sizeof(T); ++i) output.push_back(static_cast<std::uint8_t>((bits >> (i * 8U)) & 0xFFU));
}

template <typename T>
T get_le(std::span<const std::uint8_t> input, const std::size_t offset)
{
    using Unsigned = std::make_unsigned_t<T>;
    Unsigned bits = 0;
    for (std::size_t i = 0; i < sizeof(T); ++i) bits |= static_cast<Unsigned>(input[offset + i]) << (i * 8U);
    return static_cast<T>(bits);
}

struct PointD {
    double lat;
    double lon;
};

PointD to_d(const GeoPointE7 point) { return {static_cast<double>(point.latitude_e7), static_cast<double>(point.longitude_e7)}; }

GeoPointE7 to_e7(const PointD point)
{
    return {static_cast<std::int32_t>(std::llround(point.lat)), static_cast<std::int32_t>(std::llround(point.lon))};
}

// Where segment a-b crosses a tile edge. The same formula on both sides of a
// shared edge gives the same point, and the edge coordinate is exact.
PointD at_lat(const PointD a, const PointD b, const double lat)
{
    const double t = (lat - a.lat) / (b.lat - a.lat);
    return {lat, a.lon + (b.lon - a.lon) * t};
}

PointD at_lon(const PointD a, const PointD b, const double lon)
{
    const double t = (lon - a.lon) / (b.lon - a.lon);
    return {a.lat + (b.lat - a.lat) * t, lon};
}

void push_distinct(std::vector<GeoPointE7>& points, const GeoPointE7 point)
{
    if (points.empty() || !(points.back() == point)) points.push_back(point);
}

}  // namespace

bool TileGrid::valid() const
{
    if (!valid_coordinate(origin) || tile_lat_e7 <= 0 || tile_lon_e7 <= 0 || columns == 0 || rows == 0) return false;
    const std::int64_t north = static_cast<std::int64_t>(origin.latitude_e7) + static_cast<std::int64_t>(tile_lat_e7) * rows;
    const std::int64_t east = static_cast<std::int64_t>(origin.longitude_e7) + static_cast<std::int64_t>(tile_lon_e7) * columns;
    return north <= 900000000 && east <= 1800000000;
}

std::optional<TileKey> TileGrid::key_for(const GeoPointE7 point) const
{
    if (!valid()) return std::nullopt;
    const auto cell = [](const std::int64_t value, const std::int64_t start, const std::int64_t size, const int count) {
        const std::int64_t index = (value - start) >= 0 ? (value - start) / size : -1;
        return static_cast<std::uint16_t>(std::clamp<std::int64_t>(index, 0, count - 1));
    };
    return TileKey{cell(point.longitude_e7, origin.longitude_e7, tile_lon_e7, columns),
                   cell(point.latitude_e7, origin.latitude_e7, tile_lat_e7, rows)};
}

bool TileGrid::contains(const GeoPointE7 point) const
{
    if (!valid()) return false;
    const std::int64_t lat = static_cast<std::int64_t>(point.latitude_e7) - origin.latitude_e7;
    const std::int64_t lon = static_cast<std::int64_t>(point.longitude_e7) - origin.longitude_e7;
    return lat >= 0 && lon >= 0 && lat < static_cast<std::int64_t>(tile_lat_e7) * rows &&
           lon < static_cast<std::int64_t>(tile_lon_e7) * columns;
}

GeoPointE7 TileGrid::tile_minimum(const TileKey key) const
{
    return {origin.latitude_e7 + tile_lat_e7 * key.row, origin.longitude_e7 + tile_lon_e7 * key.column};
}

GeoPointE7 TileGrid::tile_maximum(const TileKey key) const
{
    return {origin.latitude_e7 + tile_lat_e7 * (key.row + 1), origin.longitude_e7 + tile_lon_e7 * (key.column + 1)};
}

GeoPointE7 TileGrid::tile_centre(const TileKey key) const
{
    const auto minimum = tile_minimum(key);
    return {minimum.latitude_e7 + tile_lat_e7 / 2, minimum.longitude_e7 + tile_lon_e7 / 2};
}

std::vector<TileKey> tiles_around(const TileGrid& grid, const TileKey centre, const int radius)
{
    std::vector<TileKey> keys;
    if (!grid.valid() || radius < 0) return keys;
    for (int row = centre.row - radius; row <= centre.row + radius; ++row) {
        if (row < 0 || row >= grid.rows) continue;
        for (int column = centre.column - radius; column <= centre.column + radius; ++column) {
            if (column < 0 || column >= grid.columns) continue;
            keys.push_back({static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)});
        }
    }
    return keys;
}

std::size_t TileArchive::declared_size(const std::span<const std::uint8_t> header)
{
    if (header.size() < header_size || !std::equal(kMagic.begin(), kMagic.end(), header.begin())) return 0;
    return get_le<std::uint32_t>(header, 32);
}

void TileArchive::close()
{
    blob_ = {};
    grid_ = {};
    entries_.clear();
}

bool TileArchive::open(const std::span<const std::uint8_t> blob)
{
    close();
    if (blob.size() < header_size || !std::equal(kMagic.begin(), kMagic.end(), blob.begin())) return false;
    if (get_le<std::uint16_t>(blob, 4) != kVersion || get_le<std::uint16_t>(blob, 6) != 0) return false;
    TileGrid grid{
        .origin = {get_le<std::int32_t>(blob, 8), get_le<std::int32_t>(blob, 12)},
        .tile_lat_e7 = get_le<std::int32_t>(blob, 16),
        .tile_lon_e7 = get_le<std::int32_t>(blob, 20),
        .columns = get_le<std::uint16_t>(blob, 24),
        .rows = get_le<std::uint16_t>(blob, 26),
    };
    const std::uint32_t count = get_le<std::uint32_t>(blob, 28);
    const std::uint32_t total = get_le<std::uint32_t>(blob, 32);
    const std::uint32_t expected_crc = get_le<std::uint32_t>(blob, 36);
    const std::size_t index_end = header_size + static_cast<std::size_t>(count) * index_entry_size;
    if (!grid.valid() || count > static_cast<std::uint32_t>(grid.columns) * grid.rows || total > blob.size() ||
        index_end > total) {
        return false;
    }
    // The CRC covers the header up to itself and the whole index.
    std::vector<std::uint8_t> covered(blob.begin(), blob.begin() + 36);
    covered.insert(covered.end(), blob.begin() + header_size, blob.begin() + static_cast<std::ptrdiff_t>(index_end));
    if (crc32(covered) != expected_crc) return false;
    std::vector<TileIndexEntry> entries;
    entries.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t at = header_size + static_cast<std::size_t>(i) * index_entry_size;
        TileIndexEntry entry{
            .key = {get_le<std::uint16_t>(blob, at), get_le<std::uint16_t>(blob, at + 2)},
            .offset = get_le<std::uint32_t>(blob, at + 4),
            .size = get_le<std::uint32_t>(blob, at + 8),
            .crc = get_le<std::uint32_t>(blob, at + 12),
        };
        if (entry.key.column >= grid.columns || entry.key.row >= grid.rows || entry.offset < index_end ||
            entry.size == 0 || static_cast<std::uint64_t>(entry.offset) + entry.size > total ||
            (!entries.empty() && !(entries.back().key < entry.key))) {
            return false;
        }
        entries.push_back(entry);
    }
    blob_ = blob.first(total);
    grid_ = grid;
    entries_ = std::move(entries);
    return true;
}

const TileIndexEntry* TileArchive::find(const TileKey key) const
{
    const auto found = std::lower_bound(entries_.begin(), entries_.end(), key,
                                        [](const TileIndexEntry& entry, const TileKey k) { return entry.key < k; });
    return found != entries_.end() && found->key == key ? &*found : nullptr;
}

bool TileArchive::load(const TileKey key, MapDataset& output) const
{
    const TileIndexEntry* entry = find(key);
    if (entry == nullptr) return false;
    const auto bytes = blob_.subspan(entry->offset, entry->size);
    if (crc32(bytes) != entry->crc) return false;
    return decode_map(bytes, output);
}

std::vector<std::uint8_t> encode_tile_archive(const TileGrid& grid, std::vector<std::pair<TileKey, MapDataset>> tiles)
{
    if (!grid.valid()) return {};
    std::sort(tiles.begin(), tiles.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<std::vector<std::uint8_t>> encoded;
    encoded.reserve(tiles.size());
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        const TileKey key = tiles[i].first;
        if (key.column >= grid.columns || key.row >= grid.rows || (i > 0 && tiles[i - 1].first == key)) return {};
        encoded.push_back(encode_map(tiles[i].second));
        if (encoded.back().empty()) return {};
    }
    std::vector<std::uint8_t> output;
    output.insert(output.end(), kMagic.begin(), kMagic.end());
    put_le(output, kVersion);
    put_le(output, static_cast<std::uint16_t>(0));
    put_le(output, grid.origin.latitude_e7);
    put_le(output, grid.origin.longitude_e7);
    put_le(output, grid.tile_lat_e7);
    put_le(output, grid.tile_lon_e7);
    put_le(output, grid.columns);
    put_le(output, grid.rows);
    put_le(output, static_cast<std::uint32_t>(tiles.size()));
    std::size_t offset = TileArchive::header_size + tiles.size() * TileArchive::index_entry_size;
    std::size_t total = offset;
    for (const auto& bytes : encoded) total += bytes.size();
    if (total > std::numeric_limits<std::uint32_t>::max()) return {};
    put_le(output, static_cast<std::uint32_t>(total));
    std::vector<std::uint8_t> index;
    for (std::size_t i = 0; i < tiles.size(); ++i) {
        put_le(index, tiles[i].first.column);
        put_le(index, tiles[i].first.row);
        put_le(index, static_cast<std::uint32_t>(offset));
        put_le(index, static_cast<std::uint32_t>(encoded[i].size()));
        put_le(index, crc32(encoded[i]));
        offset += encoded[i].size();
    }
    std::vector<std::uint8_t> covered(output.begin(), output.end());
    covered.insert(covered.end(), index.begin(), index.end());
    put_le(output, crc32(covered));
    output.insert(output.end(), index.begin(), index.end());
    for (const auto& bytes : encoded) output.insert(output.end(), bytes.begin(), bytes.end());
    return output;
}

std::vector<std::vector<GeoPointE7>> clip_polyline(const std::span<const GeoPointE7> points, const GeoPointE7 minimum,
                                                   const GeoPointE7 maximum)
{
    std::vector<std::vector<GeoPointE7>> pieces;
    std::vector<GeoPointE7> current;
    const double south = minimum.latitude_e7, north = maximum.latitude_e7;
    const double west = minimum.longitude_e7, east = maximum.longitude_e7;
    const auto finish = [&] {
        if (current.size() >= 2) pieces.push_back(std::move(current));
        current.clear();
    };
    for (std::size_t i = 1; i < points.size(); ++i) {
        const PointD a = to_d(points[i - 1]);
        const PointD b = to_d(points[i]);
        // Liang-Barsky: shrink [t0, t1] by each edge, remembering which edge cut it.
        double t0 = 0.0, t1 = 1.0;
        int edge0 = -1, edge1 = -1;
        const double d_lat = b.lat - a.lat, d_lon = b.lon - a.lon;
        const std::array<std::pair<double, double>, 4> tests{{
            {-d_lat, a.lat - south}, {d_lat, north - a.lat}, {-d_lon, a.lon - west}, {d_lon, east - a.lon},
        }};
        bool outside = false;
        for (int edge = 0; edge < 4 && !outside; ++edge) {
            const auto [p, q] = tests[static_cast<std::size_t>(edge)];
            if (p == 0.0) {
                if (q < 0.0) outside = true;
                continue;
            }
            const double r = q / p;
            if (p < 0.0) {
                if (r > t1) outside = true;
                else if (r > t0) { t0 = r; edge0 = edge; }
            } else {
                if (r < t0) outside = true;
                else if (r < t1) { t1 = r; edge1 = edge; }
            }
        }
        if (outside) {
            finish();
            continue;
        }
        const auto cut = [&](const int edge, const PointD fallback) {
            switch (edge) {
            case 0: return at_lat(a, b, south);
            case 1: return at_lat(a, b, north);
            case 2: return at_lon(a, b, west);
            case 3: return at_lon(a, b, east);
            default: return fallback;
            }
        };
        const GeoPointE7 start = edge0 < 0 ? points[i - 1] : to_e7(cut(edge0, a));
        const GeoPointE7 end = edge1 < 0 ? points[i] : to_e7(cut(edge1, b));
        if (!current.empty() && !(current.back() == start)) finish();
        push_distinct(current, start);
        push_distinct(current, end);
        if (edge1 >= 0) finish();  // left the tile
    }
    finish();
    return pieces;
}

std::vector<GeoPointE7> clip_ring(const std::span<const GeoPointE7> ring, const GeoPointE7 minimum,
                                  const GeoPointE7 maximum)
{
    std::vector<PointD> points;
    points.reserve(ring.size());
    for (const auto point : ring) points.push_back(to_d(point));
    if (points.size() > 1 && points.front().lat == points.back().lat && points.front().lon == points.back().lon) {
        points.pop_back();
    }
    const double south = minimum.latitude_e7, north = maximum.latitude_e7;
    const double west = minimum.longitude_e7, east = maximum.longitude_e7;
    std::vector<PointD> next;
    for (int edge = 0; edge < 4 && !points.empty(); ++edge) {
        const auto inside = [&](const PointD p) {
            switch (edge) {
            case 0: return p.lat >= south;
            case 1: return p.lat <= north;
            case 2: return p.lon >= west;
            default: return p.lon <= east;
            }
        };
        const auto intersect = [&](const PointD a, const PointD b) {
            switch (edge) {
            case 0: return at_lat(a, b, south);
            case 1: return at_lat(a, b, north);
            case 2: return at_lon(a, b, west);
            default: return at_lon(a, b, east);
            }
        };
        next.clear();
        for (std::size_t i = 0; i < points.size(); ++i) {
            const PointD previous = points[(i + points.size() - 1) % points.size()];
            const PointD current = points[i];
            if (inside(current)) {
                if (!inside(previous)) next.push_back(intersect(previous, current));
                next.push_back(current);
            } else if (inside(previous)) {
                next.push_back(intersect(previous, current));
            }
        }
        points.swap(next);
    }
    std::vector<GeoPointE7> output;
    output.reserve(points.size());
    for (const auto point : points) push_distinct(output, to_e7(point));
    while (output.size() > 1 && output.front() == output.back()) output.pop_back();
    // Drop slivers with no area left (a ring that only touched the tile edge).
    double twice_area = 0.0;
    for (std::size_t i = 0; i < output.size(); ++i) {
        const auto a = to_d(output[i]);
        const auto b = to_d(output[(i + 1) % output.size()]);
        twice_area += a.lon * b.lat - b.lon * a.lat;
    }
    if (output.size() < 3 || twice_area == 0.0) return {};
    return output;
}

std::vector<std::pair<TileKey, MapDataset>> split_into_tiles(const MapDataset& map, const TileGrid& grid)
{
    std::vector<std::pair<TileKey, MapDataset>> tiles;
    if (!grid.valid()) return tiles;
    std::vector<MapDataset> cells(static_cast<std::size_t>(grid.columns) * grid.rows);
    const auto cell_range = [&](const std::vector<GeoPointE7>& points) {
        GeoPointE7 low = points.front(), high = points.front();
        for (const auto point : points) {
            low = {std::min(low.latitude_e7, point.latitude_e7), std::min(low.longitude_e7, point.longitude_e7)};
            high = {std::max(high.latitude_e7, point.latitude_e7), std::max(high.longitude_e7, point.longitude_e7)};
        }
        return std::pair{*grid.key_for(low), *grid.key_for(high)};
    };
    const auto cell = [&](const int column, const int row) -> MapDataset& {
        return cells[static_cast<std::size_t>(row) * grid.columns + static_cast<std::size_t>(column)];
    };
    for (const auto& road : map.roads) {
        if (road.points.size() < 2) continue;
        const auto [low, high] = cell_range(road.points);
        for (int row = low.row; row <= high.row; ++row) {
            for (int column = low.column; column <= high.column; ++column) {
                const TileKey key{static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)};
                for (auto& piece : clip_polyline(road.points, grid.tile_minimum(key), grid.tile_maximum(key))) {
                    cell(column, row).roads.push_back({road.feature_id, road.road_class, road.name, std::move(piece)});
                }
            }
        }
    }
    for (const auto& area : map.areas) {
        if (area.rings.empty() || area.rings.front().size() < 3) continue;
        const auto [low, high] = cell_range(area.rings.front());
        for (int row = low.row; row <= high.row; ++row) {
            for (int column = low.column; column <= high.column; ++column) {
                const TileKey key{static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)};
                AreaPolygon piece{area.area_class, area.name, {}};
                for (const auto& ring : area.rings) {
                    auto clipped = clip_ring(ring, grid.tile_minimum(key), grid.tile_maximum(key));
                    if (clipped.empty()) {
                        if (piece.rings.empty()) break;  // the outline misses this tile
                        continue;
                    }
                    piece.rings.push_back(std::move(clipped));
                    if (piece.rings.size() == 255) break;
                }
                if (!piece.rings.empty()) cell(column, row).areas.push_back(std::move(piece));
            }
        }
    }
    for (int row = 0; row < grid.rows; ++row) {
        for (int column = 0; column < grid.columns; ++column) {
            MapDataset& tile = cell(column, row);
            if (tile.roads.empty() && tile.areas.empty()) continue;
            const TileKey key{static_cast<std::uint16_t>(column), static_cast<std::uint16_t>(row)};
            tile.version = 4;
            tile.origin = grid.tile_centre(key);
            tile.minimum = grid.tile_minimum(key);
            tile.maximum = grid.tile_maximum(key);
            tiles.emplace_back(key, std::move(tile));
        }
    }
    return tiles;
}

void TileWindow::set_archive(const TileArchive* archive)
{
    archive_ = archive;
    clear();
}

void TileWindow::clear()
{
    const std::uint32_t revision = map_.revision;
    map_ = {};
    map_.revision = revision + 1;
    slices_.clear();
    centre_.reset();
}

std::vector<TileKey> TileWindow::loaded_tiles() const
{
    std::vector<TileKey> keys;
    keys.reserve(slices_.size());
    for (const auto& slice : slices_) keys.push_back(slice.key);
    return keys;
}

bool TileWindow::inside_with_margin(const TileKey key, const GeoPointE7 position) const
{
    const TileGrid& grid = archive_->grid();
    const double lat_margin = hysteresis_m / kMetresPerE7;
    const double lon_margin = lat_margin / std::cos(position.latitude_e7 * 1.0e-7 * 3.14159265358979323846 / 180.0);
    const auto low = grid.tile_minimum(key);
    const auto high = grid.tile_maximum(key);
    return position.latitude_e7 >= low.latitude_e7 - lat_margin && position.latitude_e7 <= high.latitude_e7 + lat_margin &&
           position.longitude_e7 >= low.longitude_e7 - lon_margin &&
           position.longitude_e7 <= high.longitude_e7 + lon_margin;
}

bool TileWindow::update(const GeoPointE7 position)
{
    if (archive_ == nullptr || !archive_->is_open() || !valid_coordinate(position)) return false;
    const TileGrid& grid = archive_->grid();
    const auto key = grid.key_for(position);
    if (!key) return false;
    if (centre_ && (*centre_ == *key || inside_with_margin(*centre_, position))) return false;

    MapDataset next;
    next.version = 4;
    next.revision = map_.revision + 1;
    next.origin = grid.tile_centre(*key);
    next.minimum = grid.tile_minimum(*key);
    next.maximum = grid.tile_maximum(*key);
    std::vector<Slice> slices;
    for (const TileKey wanted : tiles_around(grid, *key, 1)) {
        if (archive_->find(wanted) == nullptr) continue;
        Slice slice{wanted, next.roads.size(), 0, next.areas.size(), 0};
        const auto kept = std::find_if(slices_.begin(), slices_.end(), [&](const Slice& s) { return s.key == wanted; });
        if (kept != slices_.end()) {
            // Still in range: move its features over instead of decoding again.
            std::move(map_.roads.begin() + static_cast<std::ptrdiff_t>(kept->road_begin),
                      map_.roads.begin() + static_cast<std::ptrdiff_t>(kept->road_begin + kept->road_count),
                      std::back_inserter(next.roads));
            std::move(map_.areas.begin() + static_cast<std::ptrdiff_t>(kept->area_begin),
                      map_.areas.begin() + static_cast<std::ptrdiff_t>(kept->area_begin + kept->area_count),
                      std::back_inserter(next.areas));
        } else {
            MapDataset tile;
            if (!archive_->load(wanted, tile)) {
                ++tiles_failed_;
                continue;
            }
            ++tiles_decoded_;
            std::move(tile.roads.begin(), tile.roads.end(), std::back_inserter(next.roads));
            std::move(tile.areas.begin(), tile.areas.end(), std::back_inserter(next.areas));
        }
        slice.road_count = next.roads.size() - slice.road_begin;
        slice.area_count = next.areas.size() - slice.area_begin;
        const auto low = grid.tile_minimum(wanted);
        const auto high = grid.tile_maximum(wanted);
        next.minimum = {std::min(next.minimum.latitude_e7, low.latitude_e7), std::min(next.minimum.longitude_e7, low.longitude_e7)};
        next.maximum = {std::max(next.maximum.latitude_e7, high.latitude_e7), std::max(next.maximum.longitude_e7, high.longitude_e7)};
        slices.push_back(slice);
    }
    map_ = std::move(next);
    slices_ = std::move(slices);
    centre_ = key;
    return true;
}

}  // namespace bikeos
