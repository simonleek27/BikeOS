#pragma once

#include <filesystem>

#include "bikeos/canvas.hpp"
#include "bikeos/framebuffer.hpp"

bool write_framebuffer_png(const std::filesystem::path& path,
                           const bikeos::Framebuffer& framebuffer,
                           int scale = 4);

bool write_canvas_png(const std::filesystem::path& path,
                      const bikeos::Canvas565& canvas,
                      int scale = 1);
