#pragma once

#include "planet_sandbox/project.h"

#include <cstddef>
#include <string>

namespace planet_sandbox {

Project make_solar_system_project();
BodyKind solar_body_kind(std::size_t index);
const char* solar_body_name(std::size_t index);
const char* body_kind_id(BodyKind kind);
BodyKind parse_body_kind(const std::string& id);
DesignOverrides baseline_design(BodyKind kind);

} // namespace planet_sandbox
