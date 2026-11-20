#include <cmath>
#include <algorithm>
#include <array>
#include <span>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string_view>
#include <vector>

#include "bikeos/canvas.hpp"
#include "bikeos/color_renderer.hpp"
#include "bikeos/fonts.hpp"
#include "bikeos/framebuffer.hpp"
#include "bikeos/geo.hpp"
#include "bikeos/gps.hpp"
#include "bikeos/gps_simulator.hpp"
#include "bikeos/map_data.hpp"
#include "bikeos/map_renderer.hpp"
#include "bikeos/navigation.hpp"
#include "bikeos/nmea.hpp"
#include "bikeos/protocol.hpp"

namespace {

int failures = 0;

#define CHECK(condition)                                                                            \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::cerr << __FILE__ << ':' << __LINE__ << " CHECK failed: " #condition << '\n';      \
            ++failures;                                                                             \
        }                                                                                           \
    } while (false)

bikeos::GeoPointE7 offset(const bikeos::GeoPointE7 origin, const double east_m, const double north_m)
{
    constexpr double radians = 3.14159265358979323846 / 180.0;
    constexpr double radius = 6371008.8;
    const double latitude = origin.latitude_e7 * 1.0e-7 * radians;
    return {
        .latitude_e7 = origin.latitude_e7 + static_cast<std::int32_t>(std::llround(north_m / radius / radians * 1.0e7)),
        .longitude_e7 = origin.longitude_e7 +
                        static_cast<std::int32_t>(std::llround(east_m / (radius * std::cos(latitude)) /
                                                               radians * 1.0e7)),
    };
}

bikeos::Route make_route()
{
    const bikeos::GeoPointE7 origin{.latitude_e7 = 523600000, .longitude_e7 = 48850000};
    bikeos::Route route{
        .route_id = 42,
        .version = 1,
        .points = {origin, offset(origin, 0, 100), offset(origin, 100, 100), offset(origin, 100, 200)},
        .cumulative_distance_m = {},
        .maneuvers = {
            {.point_index = 1, .type = bikeos::ManeuverType::right, .street_name = "TESTSTRAAT"},
            {.point_index = 2, .type = bikeos::ManeuverType::left, .street_name = "FIETSPAD"},
            {.point_index = 3, .type = bikeos::ManeuverType::destination, .street_name = "DESTINATION"},
        },
    };
    CHECK(route.prepare_and_validate());
    return route;
}

bikeos::GpsFix fix_at(const bikeos::GeoPointE7 position, const std::uint64_t timestamp, const float accuracy = 3.0F)
{
    return {
        .position = position,
        .utc_timestamp_ms = timestamp,
        .monotonic_timestamp_ms = timestamp,
        .speed_mps = 5.0F,
        .heading_deg = 0.0F,
        .accuracy_m = accuracy,
        .source = bikeos::GpsSource::simulated,
        .quality = 1,
        .position_valid = true,
        .heading_valid = true,
    };
}

void test_geo_and_gps_filter()
{
    const bikeos::GeoPointE7 amsterdam{523672000, 49041000};
    CHECK(bikeos::valid_coordinate(amsterdam));
    CHECK(!bikeos::valid_coordinate({910000000, 0}));
    CHECK(std::abs(bikeos::distance_m(amsterdam, offset(amsterdam, 100, 0)) - 100.0) < 0.5);

    bikeos::GpsFilter filter;
    CHECK(filter.accept(fix_at(amsterdam, 1000)).has_value());
    CHECK(!filter.accept(fix_at(offset(amsterdam, 2000, 0), 2000)).has_value());
    CHECK(!filter.stale(4000));
    CHECK(filter.stale(5001));
}

void test_nmea()
{
    bikeos::NmeaParser parser;
    constexpr std::string_view rmc = "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A";
    CHECK(bikeos::NmeaParser::checksum_valid(rmc));
    const auto fix = parser.parse(rmc, 1000);
    CHECK(fix.has_value());
    CHECK(fix->position_valid);
    CHECK(std::abs(fix->position.latitude_e7 - 481173000) < 2);
    CHECK(std::abs(fix->position.longitude_e7 - 115166667) < 2);
    CHECK(std::abs(fix->speed_mps - 11.5235F) < 0.01F);
    CHECK(!parser.parse("$GPRMC,broken*00", 2000).has_value());

    constexpr std::string_view gga = "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47";
    const auto gga_fix = parser.parse(gga, 2000);
    CHECK(gga_fix.has_value());
    CHECK(gga_fix->quality == 1);
    CHECK(std::abs(gga_fix->accuracy_m - 4.5F) < 0.01F);

    // RMC carries the date, so both sentences get a full UTC timestamp.
    CHECK(fix->utc_epoch_ms == 764426119000LL);  // 1994-03-23 12:35:19 UTC
    CHECK(gga_fix->utc_epoch_ms == 764426119000LL);
    bikeos::NmeaParser fresh;
    CHECK(fresh.parse(gga, 3000)->utc_epoch_ms == 0);  // no date seen yet
    CHECK(fresh.parse("$GPRMC,,V,,,,,,,,,,N*53", 3000) == std::nullopt);
}

void test_navigation_and_reroute()
{
    auto route = make_route();
    const auto encoded_route = bikeos::encode_route(route);
    CHECK(!encoded_route.empty());
    bikeos::Route decoded_route;
    CHECK(bikeos::decode_route(encoded_route, decoded_route));
    CHECK(decoded_route.points.size() == route.points.size());
    CHECK(decoded_route.maneuvers.size() == route.maneuvers.size());
    auto corrupt_route = encoded_route;
    corrupt_route.back() ^= 0x40;
    CHECK(!bikeos::decode_route(corrupt_route, decoded_route));

    const auto start = route.points.front();
    bikeos::NavigationEngine engine;
    CHECK(engine.set_route(route));

    auto snapshot = engine.update(fix_at(offset(start, 2, 50), 1000), true, 1000);
    CHECK(snapshot.state == bikeos::NavigationState::navigating);
    CHECK(snapshot.cross_track_distance_m < 3.0F);
    CHECK(snapshot.distance_to_maneuver_m > 45.0F && snapshot.distance_to_maneuver_m < 55.0F);
    CHECK(snapshot.next_maneuver->type == bikeos::ManeuverType::right);
    CHECK(snapshot.following_maneuver.has_value());
    CHECK(snapshot.following_maneuver->type == bikeos::ManeuverType::left);
    CHECK(std::abs(snapshot.following_maneuver_gap_m - 100.0F) < 1.0F);

    engine.set_run_state(bikeos::NavigationRunState::paused);
    const float paused_progress = engine.snapshot().route_progress_m;
    snapshot = engine.update(fix_at(offset(start, 90, 100), 1500), true, 1500);
    CHECK(snapshot.run_state == bikeos::NavigationRunState::paused);
    CHECK(snapshot.route_progress_m == paused_progress);
    engine.set_run_state(bikeos::NavigationRunState::running);

    for (std::uint64_t tick = 2; tick <= 6; ++tick) {
        snapshot = engine.update(fix_at(offset(start, 45, 60), tick * 1000), true, tick * 1000);
    }
    CHECK(snapshot.state == bikeos::NavigationState::rerouting);
    CHECK(snapshot.reroute_requested);
    snapshot = engine.update(fix_at(offset(start, 45, 65), 7000), true, 7000);
    CHECK(!snapshot.reroute_requested);

    engine.set_phone_connected(false);
    CHECK(engine.snapshot().state == bikeos::NavigationState::phone_disconnected);
    CHECK(!engine.snapshot().phone_connected);
    engine.set_phone_connected(true);
    CHECK(engine.snapshot().phone_connected);
}

void test_map_codec_and_rendering()
{
    const auto route = make_route();
    bikeos::MapDataset map{
        .version = 3,
        .origin = route.points.front(),
        .minimum = offset(route.points.front(), -200, -200),
        .maximum = offset(route.points.front(), 200, 300),
        .roads = {
            {.feature_id = 1234,
             .road_class = bikeos::RoadClass::residential,
             .name = "TESTSTRAAT",
             .points = {offset(route.points.front(), -100, 100), offset(route.points.front(), 150, 100)}},
            {.feature_id = 5678,
             .road_class = bikeos::RoadClass::cycleway,
             .name = "FIETSPAD",
             .points = {offset(route.points.front(), 0, -100), offset(route.points.front(), 0, 250)}},
        },
    };
    const auto encoded = bikeos::encode_map(map);
    CHECK(!encoded.empty());
    bikeos::MapDataset decoded;
    CHECK(bikeos::decode_map(encoded, decoded));
    CHECK(decoded.point_count() == 4);
    CHECK(decoded.version == 4);
    CHECK(decoded.areas.empty());
    CHECK(decoded.roads[0].feature_id == 1234);
    CHECK(decoded.roads[0].name == "TESTSTRAAT");
    CHECK(decoded.roads[1].feature_id == 5678);
    CHECK(decoded.roads[1].name == "FIETSPAD");
    auto corrupt = encoded;
    corrupt.back() ^= 0x80;
    CHECK(!bikeos::decode_map(corrupt, decoded));

    bikeos::NavigationEngine engine;
    CHECK(engine.set_route(route));
    const auto nav = engine.update(fix_at(offset(route.points.front(), 0, 80), 1000), true, 1000);
    bikeos::Framebuffer framebuffer;
    bikeos::MapRenderer renderer;
    renderer.render(framebuffer, map, route.points, nav,
                    {.center = nav.position, .meters_per_pixel = 4.0F});
    std::size_t pixels = 0;
    for (int y = 0; y < bikeos::Framebuffer::height; ++y) {
        for (int x = 0; x < bikeos::Framebuffer::width; ++x) pixels += framebuffer.pixel(x, y) ? 1U : 0U;
    }
    CHECK(pixels > 500);
    CHECK(pixels < 7000);
    CHECK(std::abs(bikeos::MapRenderer::zoom_for_distance(250.0F) - 6.0F) < 0.01F);
    CHECK(std::abs(bikeos::MapRenderer::zoom_for_distance(100.0F) - 4.5F) < 0.01F);
    CHECK(std::abs(bikeos::MapRenderer::zoom_for_distance(50.0F) - 3.2F) < 0.01F);
    CHECK(std::abs(bikeos::MapRenderer::zoom_for_distance(10.0F) - 2.2F) < 0.01F);
    renderer.render(framebuffer, map, route.points, nav,
                    {.center = nav.position, .meters_per_pixel = 4.0F},
                    {.mode = bikeos::DisplayMode::turn_now});
    CHECK(bikeos::crc32(framebuffer.bytes()) != 0);
    framebuffer.set_pixel(-1, -1);
    framebuffer.set_pixel(128, 64);
    CHECK(framebuffer.bytes().size() == 1024);
}

void test_heading_up_rotation()
{
    // Riding east with a road due east: heading-up must draw that road above the rider, not below.
    const bikeos::GeoPointE7 origin{.latitude_e7 = 523700000, .longitude_e7 = 48900000};
    const std::vector<bikeos::GeoPointE7> route{origin, offset(origin, 200, 0)};
    bikeos::NavigationSnapshot nav;
    nav.gps_valid = true;
    nav.position = origin;
    nav.heading_deg = 90.0F;
    nav.heading_valid = true;
    bikeos::Framebuffer framebuffer;
    bikeos::MapRenderer renderer;
    renderer.render(framebuffer, bikeos::MapDataset{}, route, nav,
                    {.center = origin, .meters_per_pixel = 4.0F, .heading_up = true, .automatic_zoom = false},
                    {.show_status = false});
    const int centre_x = bikeos::Framebuffer::width / 2;
    CHECK(framebuffer.pixel(centre_x, 14));
    CHECK(!framebuffer.pixel(centre_x, 48));
    CHECK(!framebuffer.pixel(centre_x + 30, 31));
}

void test_simulated_ride()
{
    auto route = make_route();
    bikeos::GpsSimulator simulator(1234);
    CHECK(simulator.set_route(route));
    simulator.set_speed_mps(6.0F);
    simulator.set_events({
        {.start_progress_m = 130, .end_progress_m = 180,
         .type = bikeos::SimulationEventType::off_route, .magnitude_m = 45},
    });
    bikeos::NavigationEngine engine;
    CHECK(engine.set_route(route));
    bool saw_off_route = false;
    for (std::uint64_t second = 1; second < 80 && !simulator.finished(); ++second) {
        const auto fix = simulator.step(second * 1000);
        const auto nav = engine.update(fix, true, second * 1000);
        saw_off_route |= nav.state == bikeos::NavigationState::off_route ||
                         nav.state == bikeos::NavigationState::rerouting;
    }
    CHECK(saw_off_route);
    CHECK(simulator.finished());
}

void test_amsterdam_assets()
{
    const auto read = [](const std::filesystem::path& path) {
        std::ifstream input(path, std::ios::binary);
        return std::vector<std::uint8_t>(std::istreambuf_iterator<char>(input), {});
    };
    bikeos::MapDataset map;
    bikeos::Route route;
    CHECK(bikeos::decode_map(read(BIKEOS_SOURCE_DIR "/data/amsterdam_demo.bmap"), map));
    CHECK(bikeos::decode_route(read(BIKEOS_SOURCE_DIR "/data/amsterdam_demo.brte"), route));
    CHECK(map.version == 4);
    CHECK(map.roads.size() == 1017);
    CHECK(map.areas.size() > 50);
    CHECK(std::any_of(map.areas.begin(), map.areas.end(),
                      [](const auto& area) { return area.area_class == bikeos::AreaClass::water; }));
    CHECK(map.point_count() == 2936);
    CHECK(route.points.size() == 84);
    CHECK(route.maneuvers.size() == 5);
    CHECK(route.total_distance_m > 1400.0F && route.total_distance_m < 1420.0F);
    CHECK(std::any_of(map.roads.begin(), map.roads.end(), [](const auto& road) { return !road.name.empty(); }));
    CHECK(std::all_of(route.maneuvers.begin(), route.maneuvers.end(), [](const auto& maneuver) {
        return !maneuver.street_name.empty() && maneuver.street_name != "Destination";
    }));

    bikeos::NavigationEngine navigation;
    CHECK(navigation.set_route(route));
    const auto snapshot = navigation.update(fix_at(route.points.front(), 1000), true, 1000);
    bikeos::Framebuffer framebuffer;
    bikeos::MapRenderer renderer;
    renderer.render(framebuffer, map, route.points, snapshot,
                    {.center = route.points.front(), .meters_per_pixel = 4.0F});
    CHECK(bikeos::crc32(framebuffer.bytes()) != 0);
}

void test_ble_protocol()
{
    using namespace bikeos::protocol;
    const std::vector<std::uint8_t> bytes{1, 2, 3, 4, 5};
    const FrameHeader header{.type = MessageType::transfer_chunk, .transfer_id = 7, .sequence = 2, .flags = 1};
    const auto encoded = encode_frame(header, encode_transfer_chunk(12, bytes));
    CHECK(encoded.size() == frame_header_size + sizeof(std::uint32_t) + bytes.size());
    DecodedFrame decoded;
    CHECK(decode_frame(encoded, decoded));
    CHECK(decoded.header.transfer_id == 7);
    TransferChunk chunk;
    CHECK(decode_transfer_chunk(decoded.payload, chunk));
    CHECK(chunk.offset == 12);
    CHECK(std::equal(chunk.bytes.begin(), chunk.bytes.end(), bytes.begin(), bytes.end()));

    auto corrupt = encoded;
    corrupt.back() ^= 0x80;
    CHECK(!decode_frame(corrupt, decoded));
    auto truncated = encoded;
    truncated.pop_back();
    CHECK(!decode_frame(truncated, decoded));

    const TransferBegin begin{.bundle_id = 9,
                              .route_id = 42,
                              .map_size = 1024,
                              .route_size = 256,
                              .map_crc = 1,
                              .route_crc = 2,
                              .bundle_crc = 3};
    TransferBegin decoded_begin;
    CHECK(decode_transfer_begin(encode_transfer_begin(begin), decoded_begin));
    CHECK(decoded_begin.bundle_id == 9 && decoded_begin.route_size == 256);
    const auto status_payload = encode_status(Status{.navigation_state = NavigationState::navigating,
                                                     .gps_source = GpsSource::simulated,
                                                     .flags = 3,
                                                     .latitude_e7 = 523600000,
                                                     .longitude_e7 = 48850000,
                                                     .active_route_id = 42,
                                                     .route_remaining_m = 123,
                                                     .next_maneuver_m = 18,
                                                     .free_heap = 999});
    Status status;
    CHECK(decode_status(status_payload, status));
    CHECK(status.navigation_state == NavigationState::navigating && status.next_maneuver_m == 18);

    const auto paused_payload = encode_status(Status{.navigation_state = NavigationState::navigating,
                                                     .gps_source = GpsSource::simulated,
                                                     .flags = 3,
                                                     .active_route_id = 42,
                                                     .route_remaining_m = 123,
                                                     .run_state = RunState::paused});
    Status paused_status;
    CHECK(decode_status(paused_payload, paused_status));
    CHECK(paused_status.run_state == RunState::paused);

    const auto command = encode_frame(
        {.type = MessageType::trigger_off_route, .transfer_id = 0, .sequence = 8, .flags = 0}, {});
    CHECK(decode_frame(command, decoded));
    CHECK(decoded.header.type == MessageType::trigger_off_route);
}

void test_ble_fragment_transport()
{
    using namespace bikeos::protocol;
    const std::vector<std::uint8_t> payload(180, 0xA5);
    const auto frame = encode_frame({.type = MessageType::transfer_chunk, .transfer_id = 17, .sequence = 4},
                                    encode_transfer_chunk(0, payload));
    CHECK(frame.size() > 20);

    for (const std::size_t write_limit : {std::size_t{20}, std::size_t{23}, std::size_t{244}, std::size_t{247}}) {
        BleFragmentReassembler reassembler;
        std::vector<std::uint8_t> reconstructed;
        std::vector<std::uint8_t> final_frame;
        std::size_t offset = 0;
        std::uint16_t message_id = static_cast<std::uint16_t>(write_limit);
        while (offset < frame.size()) {
            const std::size_t count = std::min(write_limit - ble_fragment_header_size, frame.size() - offset);
            const auto fragment = encode_ble_fragment(message_id,
                                                      static_cast<std::uint16_t>(frame.size()),
                                                      static_cast<std::uint16_t>(offset),
                                                      std::span<const std::uint8_t>(frame).subspan(offset, count));
            CHECK(!fragment.empty());
            const auto result = reassembler.append(fragment, reconstructed);
            if (offset + count < frame.size()) CHECK(result == BleFragmentResult::incomplete);
            offset += count;
        }
        CHECK(reconstructed == frame);
        DecodedFrame decoded;
        CHECK(decode_frame(reconstructed, decoded));

        const auto bad_order = encode_ble_fragment(message_id + 1,
                                                   static_cast<std::uint16_t>(frame.size()),
                                                   1,
                                                   std::span<const std::uint8_t>(frame).subspan(1, 1));
        CHECK(!bad_order.empty());
        CHECK(reassembler.append(bad_order, final_frame) == BleFragmentResult::invalid);
    }
}


void test_map_areas_codec()
{
    const bikeos::GeoPointE7 origin{523600000, 48850000};
    bikeos::MapDataset map{
        .version = 4,
        .origin = origin,
        .minimum = offset(origin, -300, -300),
        .maximum = offset(origin, 300, 300),
        .roads = {{.feature_id = 1, .road_class = bikeos::RoadClass::residential, .name = "Kade",
                   .points = {offset(origin, -50, 0), offset(origin, 50, 0)}}},
        .areas = {
            {.area_class = bikeos::AreaClass::water, .name = "Prinsengracht",
             .rings = {{offset(origin, -100, -100), offset(origin, 100, -100), offset(origin, 100, 100),
                        offset(origin, -100, 100)},
                       {offset(origin, -10, -10), offset(origin, 10, -10), offset(origin, 0, 10)}}},
            {.area_class = bikeos::AreaClass::park, .name = "",
             .rings = {{offset(origin, 150, 150), offset(origin, 200, 150), offset(origin, 200, 200)}}},
        },
    };
    const auto encoded = bikeos::encode_map(map);
    CHECK(!encoded.empty());
    bikeos::MapDataset decoded;
    CHECK(bikeos::decode_map(encoded, decoded));
    CHECK(decoded.areas.size() == 2);
    CHECK(decoded.areas[0].area_class == bikeos::AreaClass::water);
    CHECK(decoded.areas[0].name == "Prinsengracht");
    CHECK(decoded.areas[0].rings.size() == 2);
    CHECK(decoded.areas[0].rings[1].size() == 3);
    CHECK(decoded.areas[1].area_class == bikeos::AreaClass::park);

    // A v3 file (no area section) must still decode: drop the area count and
    // rewrite version, payload size and CRC the way the old encoder did.
    map.areas.clear();
    auto v3 = bikeos::encode_map(map);
    v3.resize(v3.size() - 2);
    v3[4] = 3;
    const std::uint32_t payload = static_cast<std::uint32_t>(v3.size() - 48);
    const std::uint32_t crc = bikeos::crc32(std::span<const std::uint8_t>(v3).subspan(48));
    for (int i = 0; i < 4; ++i) {
        v3[40 + i] = static_cast<std::uint8_t>(payload >> (8 * i));
        v3[44 + i] = static_cast<std::uint8_t>(crc >> (8 * i));
    }
    CHECK(bikeos::decode_map(v3, decoded));
    CHECK(decoded.version == 3);
    CHECK(decoded.roads.size() == 1);
    CHECK(decoded.areas.empty());
}

void test_canvas_and_fonts()
{
    bikeos::Canvas565 canvas(40, 30);
    canvas.clear(0);
    const std::array<bikeos::PointF, 4> square{{{10, 10}, {30, 10}, {30, 20}, {10, 20}}};
    canvas.fill_polygon(square, 0xFFFF);
    std::size_t filled = 0;
    for (const auto pixel : canvas.pixels()) filled += pixel == 0xFFFF ? 1U : 0U;
    CHECK(filled == 200);

    const std::array<bikeos::PointF, 4> hole{{{15, 12}, {25, 12}, {25, 18}, {15, 18}}};
    const std::array<std::span<const bikeos::PointF>, 2> rings{square, hole};
    canvas.clear(0);
    canvas.fill_polygon(rings, 0xFFFF);
    CHECK(canvas.pixel(12, 15) == 0xFFFF);
    CHECK(canvas.pixel(20, 15) == 0);  // inside the hole

    CHECK(bikeos::blend565(0x0000, 0xFFFF, 0) == 0x0000);
    CHECK(bikeos::blend565(0x0000, 0xFFFF, 255) == 0xFFFF);
    const auto half = bikeos::blend565(0x0000, bikeos::rgb565(255, 255, 255), 128);
    CHECK(((half >> 11U) & 0x1FU) >= 15 && ((half >> 11U) & 0x1FU) <= 16);

    canvas.clear(0);
    canvas.thick_line({5, 5}, {35, 5}, 4.0F, 0xFFFF);
    CHECK(canvas.pixel(20, 5) == 0xFFFF);
    CHECK(canvas.pixel(20, 12) == 0);
    canvas.set_pixel(-1, -1, 0xFFFF);  // clipped, no crash
    canvas.set_pixel(40, 30, 0xFFFF);

    std::size_t index = 0;
    CHECK(bikeos::next_codepoint("\xC3\xA9", index) == 0xE9);  // é
    CHECK(index == 2);
    index = 0;
    CHECK(bikeos::next_codepoint("\xC3", index) == '?');
    CHECK(bikeos::font_title.find('A') != nullptr);
    CHECK(bikeos::font_title.find(0xEB) != nullptr);  // ë in Dutch street names
    CHECK(bikeos::font_distance.find('A') == nullptr);
    CHECK(bikeos::text_width(bikeos::font_title, "Prinsengracht") > 60);
    const auto fitted = bikeos::fit_text(bikeos::font_title, "Stadhouderskade Stadhouderskade", 90);
    CHECK(bikeos::text_width(bikeos::font_title, fitted) <= 90);
    CHECK(fitted.ends_with("\xE2\x80\xA6"));
    CHECK(bikeos::fit_text(bikeos::font_title, "Kade", 90) == "Kade");

    bikeos::Canvas565 text_canvas(60, 20);
    text_canvas.clear(0);
    CHECK(text_canvas.draw_text(bikeos::font_title, 2, 15, "Hi", 0xFFFF) > 10);
    std::size_t ink = 0;
    for (const auto pixel : text_canvas.pixels()) ink += pixel != 0 ? 1U : 0U;
    CHECK(ink > 20);
}

void test_time_and_theme()
{
    constexpr std::int64_t hour = 3600000;
    CHECK(bikeos::amsterdam_local_ms(1782907200000LL) - 1782907200000LL == 2 * hour);  // July: CEST
    CHECK(bikeos::amsterdam_local_ms(1768478400000LL) - 1768478400000LL == 1 * hour);  // January: CET
    CHECK(bikeos::amsterdam_local_ms(1774745940000LL) - 1774745940000LL == 1 * hour);  // 2026-03-29 00:59 UTC
    CHECK(bikeos::amsterdam_local_ms(1774746000000LL) - 1774746000000LL == 2 * hour);  // 01:00 UTC switch

    const bikeos::GeoPointE7 amsterdam{523600000, 48850000};
    const double noon = bikeos::solar_elevation_deg(1782042000000LL, amsterdam);    // 2026-06-21 11:40 UTC
    const double night = bikeos::solar_elevation_deg(1782085200000LL, amsterdam);   // 2026-06-21 23:40 UTC
    CHECK(noon > 58.0 && noon < 64.0);
    CHECK(night < -5.0);
    CHECK(!bikeos::night_theme({.utc_epoch_ms = 1795091400000LL}, amsterdam));  // 2026-11-19 12:30 UTC
    CHECK(bikeos::night_theme({.utc_epoch_ms = 1795118400000LL}, amsterdam));   // 20:00 UTC
    CHECK(bikeos::night_theme({}, amsterdam));  // no clock yet
    CHECK(!bikeos::night_theme({.theme = bikeos::ThemeMode::day}, amsterdam));

    CHECK(bikeos::format_distance(45.0F) == "45 m");
    CHECK(bikeos::format_distance(183.0F) == "180 m");
    CHECK(bikeos::format_distance(996.0F) == "1.0 km");
    CHECK(bikeos::format_distance(1234.0F) == "1.2 km");
    CHECK(bikeos::format_distance(12345.0F) == "12 km");
}

void test_color_renderer()
{
    const auto route = make_route();
    bikeos::MapDataset map{
        .version = 4,
        .origin = route.points.front(),
        .minimum = offset(route.points.front(), -300, -300),
        .maximum = offset(route.points.front(), 300, 400),
        .roads = {{.feature_id = 1, .road_class = bikeos::RoadClass::cycleway, .name = "Fietspad",
                   .points = {offset(route.points.front(), -50, -100), offset(route.points.front(), -50, 250)}}},
        .areas = {{.area_class = bikeos::AreaClass::water, .name = "Gracht",
                   .rings = {{offset(route.points.front(), 30, 0), offset(route.points.front(), 60, 0),
                              offset(route.points.front(), 60, 200), offset(route.points.front(), 30, 200)}}}},
    };
    bikeos::NavigationEngine engine;
    CHECK(engine.set_route(route));
    const auto nav = engine.update(fix_at(offset(route.points.front(), 0, 20), 1000), true, 1000);
    bikeos::Canvas565 canvas;
    bikeos::ColorMapRenderer renderer;
    renderer.render(canvas, map, route.points, nav, {.utc_epoch_ms = 1795091400000LL});  // daytime
    const auto card = renderer.card_rect();
    CHECK(card.x == 6 && card.y == 6 && card.width >= 150 && card.height >= 70);
    CHECK(canvas.pixel(card.x + card.width / 2, card.y + 3) == bikeos::rgb565(0xFF, 0xFF, 0xFF));  // day card
    std::size_t magenta = 0;
    for (const auto pixel : canvas.pixels()) magenta += pixel == bikeos::rgb565(0xD6, 0x00, 0x6F) ? 1U : 0U;
    CHECK(magenta > 200);  // the route ahead
    // Turn is 80 m ahead: the zoom eases in towards the turn level.
    for (int i = 0; i < 10; ++i) renderer.render(canvas, map, route.points, nav, {});
    CHECK(renderer.meters_per_pixel() < 1.0F);
    CHECK(canvas.pixel(card.x + card.width / 2, card.y + 3) == bikeos::rgb565(0x1F, 0x24, 0x2B));  // night, no clock

    bikeos::NavigationEngine lost;
    CHECK(lost.set_route(route));
    lost.update(fix_at(offset(route.points.front(), 0, 30), 1000), true, 1000);
    const auto gone = lost.update(std::nullopt, true, 9000);
    CHECK(gone.state == bikeos::NavigationState::gps_lost);
    bikeos::ColorMapRenderer fresh;
    fresh.render(canvas, map, route.points, gone, {.theme = bikeos::ThemeMode::day});
    CHECK(canvas.pixel(fresh.card_rect().x + 20, fresh.card_rect().y + 3) == bikeos::rgb565(0xFB, 0xD0, 0xD0));
}

}  // namespace

int main()
{
    test_geo_and_gps_filter();
    test_nmea();
    test_navigation_and_reroute();
    test_map_codec_and_rendering();
    test_heading_up_rotation();
    test_simulated_ride();
    test_amsterdam_assets();
    test_ble_protocol();
    test_ble_fragment_transport();
    test_map_areas_codec();
    test_canvas_and_fonts();
    test_time_and_theme();
    test_color_renderer();
    if (failures != 0) {
        std::cerr << failures << " test assertion(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "All BikeOS core tests passed\n";
    return EXIT_SUCCESS;
}
