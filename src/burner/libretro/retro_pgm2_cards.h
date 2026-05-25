#ifndef RETRO_PGM2_CARDS_H
#define RETRO_PGM2_CARDS_H

#pragma once

#include <vector>

struct retro_core_option_v2_definition;

void retro_pgm2_cards_reset();
void retro_pgm2_cards_push_options(std::vector<const retro_core_option_v2_definition*>& vars_systems);
void retro_pgm2_cards_refresh_environment();
void retro_pgm2_cards_create_variables();
void retro_pgm2_cards_apply_variables();
void retro_pgm2_cards_save_files();

#endif
