#pragma once

#include <filesystem>

#include "bikeos/framebuffer.hpp"

bool write_framebuffer_png(const std::filesystem::path& path,
                           const bikeos::Framebuffer& framebuffer,
                           int scale = 4);
