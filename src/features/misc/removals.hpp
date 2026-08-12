/*
data: 2026-08-12
file: src/features/misc/removals.hpp
author: HappyKuro
*/
#ifndef REMOVALS_HPP
#define REMOVALS_HPP

namespace removals
{

void on_create_move();

[[nodiscard]] bool should_skip_model(const char* model_name);

}

#endif
