#include <SDL.h>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <span>
#include <string>
#include <thread>
#include <vector>

#include "bikeos/framebuffer.hpp"
#include "bikeos/gps_simulator.hpp"
#include "bikeos/map_data.hpp"
#include "bikeos/map_renderer.hpp"
#include "bikeos/navigation.hpp"
#include "png_writer.hpp"

namespace {

std::vector<std::uint8_t> read_binary(const std::filesystem::path& path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

bool load_assets(const std::filesystem::path& data_dir, bikeos::MapDataset& map, bikeos::Route& route)
{
    const auto map_bytes = read_binary(data_dir / "amsterdam_demo.bmap");
    const auto route_bytes = read_binary(data_dir / "amsterdam_demo.brte");
    return !map_bytes.empty() && !route_bytes.empty() && bikeos::decode_map(map_bytes, map) &&
           bikeos::decode_route(route_bytes, route);
}

void render_pixels(SDL_Renderer* renderer, const bikeos::Framebuffer& framebuffer, const int scale)
{
    SDL_SetRenderDrawColor(renderer, 9, 40, 94, 255);
    SDL_RenderClear(renderer);
    SDL_SetRenderDrawColor(renderer, 238, 246, 255, 255);
    for (int y = 0; y < bikeos::Framebuffer::height; ++y) {
        for (int x = 0; x < bikeos::Framebuffer::width; ++x) {
            if (!framebuffer.pixel(x, y)) continue;
            const SDL_Rect pixel{x * scale, y * scale, scale, scale};
            SDL_RenderFillRect(renderer, &pixel);
        }
    }
    SDL_RenderPresent(renderer);
}

void render_frame(bikeos::Framebuffer& framebuffer,
                  const bikeos::MapDataset& map,
                  const bikeos::Route& route,
                  const bikeos::NavigationSnapshot& navigation,
                  const float zoom,
                  const bool show_route)
{
    bikeos::MapRenderer renderer;
    renderer.render(framebuffer, map, route.points, navigation,
                    {.center = navigation.gps_valid ? navigation.position : route.points.front(),
                     .meters_per_pixel = zoom,
                     .heading_up = false,
                     .heading_deg = navigation.heading_deg},
                    {.show_route = show_route, .show_status = true});
}

int export_snapshots(const std::filesystem::path& output_dir,
                     const bikeos::MapDataset& map,
                     bikeos::Route route)
{
    std::filesystem::create_directories(output_dir);
    bikeos::Framebuffer framebuffer;
    bikeos::NavigationEngine navigation;
    bikeos::GpsSimulator simulator(0xB14E05U);
    navigation.set_route(route);
    simulator.set_route(route);
    simulator.set_speed_mps(7.0F);

    std::uint64_t now = 1000;
    std::optional<bikeos::GpsFix> fix;
    for (int step = 0; step < 24; ++step) {
        fix = simulator.step(now);
        navigation.update(fix, true, now);
        now += 1000;
    }
    render_frame(framebuffer, map, route, navigation.snapshot(), 4.0F, true);
    if (!write_framebuffer_png(output_dir / "navigating.png", framebuffer, 6)) return 1;

    simulator.set_events({{simulator.progress_m(), simulator.progress_m() + 90.0F,
                           bikeos::SimulationEventType::off_route, 55.0F}});
    for (int step = 0; step < 6; ++step) {
        fix = simulator.step(now);
        navigation.update(fix, true, now);
        now += 1000;
    }
    render_frame(framebuffer, map, route, navigation.snapshot(), 4.0F, true);
    if (!write_framebuffer_png(output_dir / "rerouting.png", framebuffer, 6)) return 1;

    navigation.update(std::nullopt, true, now);
    render_frame(framebuffer, map, route, navigation.snapshot(), 4.0F, true);
    if (!write_framebuffer_png(output_dir / "gps-lost.png", framebuffer, 6)) return 1;

    const bikeos::GpsFix arrival{
        .position = route.points.back(),
        .utc_timestamp_ms = now,
        .monotonic_timestamp_ms = now,
        .speed_mps = 0.0F,
        .heading_deg = 0.0F,
        .accuracy_m = 3.0F,
        .source = bikeos::GpsSource::simulated,
        .quality = 1,
        .position_valid = true,
        .heading_valid = false,
    };
    navigation.update(arrival, true, now);
    render_frame(framebuffer, map, route, navigation.snapshot(), 4.0F, true);
    if (!write_framebuffer_png(output_dir / "arrived.png", framebuffer, 6)) return 1;
    std::cout << "Exported BikeOS framebuffer snapshots to " << output_dir << '\n';
    return 0;
}

}  // namespace

int main(int argc, char** argv)
{
    std::filesystem::path data_dir = BIKEOS_SOURCE_DIR "/data";
    std::optional<std::filesystem::path> export_dir;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--data" && i + 1 < argc) data_dir = argv[++i];
        else if (argument == "--export-snapshots" && i + 1 < argc) export_dir = argv[++i];
        else {
            std::cerr << "Usage: bikeos_simulator [--data DIR] [--export-snapshots DIR]\n";
            return 2;
        }
    }

    bikeos::MapDataset map;
    bikeos::Route route;
    if (!load_assets(data_dir, map, route)) {
        std::cerr << "Failed to validate map/route assets in " << data_dir << '\n';
        return 1;
    }
    if (export_dir) return export_snapshots(*export_dir, map, route);

    constexpr int scale = 8;
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) {
        std::cerr << "SDL initialization failed: " << SDL_GetError() << '\n';
        return 1;
    }
    SDL_Window* window = SDL_CreateWindow("BikeOS — real OSM moving-map simulator",
                                           SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                           bikeos::Framebuffer::width * scale,
                                           bikeos::Framebuffer::height * scale,
                                           SDL_WINDOW_ALLOW_HIGHDPI);
    SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
    if (!window || !renderer) {
        std::cerr << "SDL window failed: " << SDL_GetError() << '\n';
        SDL_Quit();
        return 1;
    }

    bikeos::NavigationEngine navigation;
    bikeos::GpsSimulator simulator(0xB14E05U);
    navigation.set_route(route);
    simulator.set_route(route);
    simulator.set_speed_mps(6.0F);
    simulator.set_events({
        {route.total_distance_m * 0.44F, route.total_distance_m * 0.50F,
         bikeos::SimulationEventType::off_route, 50.0F},
        {route.total_distance_m * 0.70F, route.total_distance_m * 0.74F,
         bikeos::SimulationEventType::gps_loss, 0.0F},
    });

    bikeos::Framebuffer framebuffer;
    bool running = true;
    bool paused = false;
    bool show_route = true;
    float zoom = 4.0F;
    float speed_multiplier = 1.0F;
    std::uint64_t simulation_ms = 0;
    auto last_step = std::chrono::steady_clock::now();
    std::optional<bikeos::GpsFix> fix;

    while (running) {
        SDL_Event event{};
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_QUIT) running = false;
            if (event.type != SDL_KEYDOWN) continue;
            switch (event.key.keysym.sym) {
                case SDLK_ESCAPE: running = false; break;
                case SDLK_SPACE: paused = !paused; break;
                case SDLK_r:
                    simulator.reset();
                    navigation.set_route(route);
                    simulation_ms = 0;
                    break;
                case SDLK_EQUALS:
                case SDLK_PLUS: speed_multiplier = std::min(8.0F, speed_multiplier * 2.0F); break;
                case SDLK_MINUS: speed_multiplier = std::max(0.25F, speed_multiplier / 2.0F); break;
                case SDLK_LEFTBRACKET: zoom = std::min(12.0F, zoom + 0.5F); break;
                case SDLK_RIGHTBRACKET: zoom = std::max(1.0F, zoom - 0.5F); break;
                case SDLK_v: show_route = !show_route; break;
                case SDLK_o:
                    simulator.set_events({{simulator.progress_m(), simulator.progress_m() + 90.0F,
                                           bikeos::SimulationEventType::off_route, 55.0F}});
                    break;
                case SDLK_g:
                    simulator.set_events({{simulator.progress_m(), simulator.progress_m() + 55.0F,
                                           bikeos::SimulationEventType::gps_loss, 0.0F}});
                    break;
                case SDLK_s: write_framebuffer_png("bikeos-screenshot.png", framebuffer, 6); break;
                default: break;
            }
        }

        const auto now = std::chrono::steady_clock::now();
        const auto elapsed = std::chrono::duration<float>(now - last_step).count();
        if (!paused && elapsed >= 1.0F / speed_multiplier) {
            simulation_ms += 1000;
            fix = simulator.step(simulation_ms);
            navigation.update(fix, true, simulation_ms);
            last_step = now;
        }
        render_frame(framebuffer, map, route, navigation.snapshot(), zoom, show_route);
        render_pixels(renderer, framebuffer, scale);

        const auto& state = navigation.snapshot();
        const std::string title = "BikeOS | " + std::string(bikeos::navigation_state_name(state.state)) +
                                  " | " + std::to_string(static_cast<int>(state.route_remaining_m)) +
                                  " m left | SPACE pause, R reset, +/- speed, [/] zoom, O off-route, G GPS loss";
        SDL_SetWindowTitle(window, title.c_str());
        std::this_thread::sleep_for(std::chrono::milliseconds(8));
    }

    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
