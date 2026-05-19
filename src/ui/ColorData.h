// This file is part of WrathClassicAPI.
//
// WrathClassicAPI is free software: you can redistribute it and/or modify it under the terms
// of the GNU Lesser General Public License as published by the Free Software Foundation, either
// version 3 of the License, or (at your option) any later version.
//
// WrathClassicAPI is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
// PURPOSE. See the GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License along with
// WrathClassicAPI. If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include <cstdint>

// Snapshot of `GlobalColor.dbc` from BC Classic 2.5.5 (build 67323).
// WotLK 3.3.5a has `GlobalColor.dbc` but no `C_UIColor` Lua namespace
// (that surface was added in 8.0 / BfA). We embed Blizzard's modern
// color table verbatim and expose it via `C_UIColor.GetColors` so an
// addon side can loop it the same way modern Anniversary's
// `Blizzard_SharedXMLBase/Color.lua` does:
//
//   for _, dbColor in ipairs(C_UIColor.GetColors()) do
//       _G[dbColor.baseTag]          = CreateColor(dbColor.color:GetRGBA())
//       _G[dbColor.baseTag.."_CODE"] = color:GenerateHexColorMarkup()
//   end
//
// Each `argb` field is the signed `Color` column from the DBC. Decode
// as `uint32_t` and split into 4 bytes:
//   uint32_t v = static_cast<uint32_t>(argb);
//   a = ((v >> 24) & 0xFF) / 255.0
//   r = ((v >> 16) & 0xFF) / 255.0
//   g = ((v >>  8) & 0xFF) / 255.0
//   b = ( v        & 0xFF) / 255.0
//
// Duplicate baseTags in the source DBC (PURE_RED_COLOR, INVASION_*,
// AREA_DESCRIPTION_FONT_COLOR, REFUND_TOOLTIP_COLOR) have been
// deduplicated keeping the higher-ID entry — that's the value modern
// WoW sees after iterating `ipairs(DBColors)` to its end.

namespace UI::ColorData {

struct Entry {
    const char *baseTag;
    int32_t argb;
};

constexpr Entry kColors[] = {
    {"NORMAL_FONT_COLOR",                                -12032},
    {"WHITE_FONT_COLOR",                                 -1},
    {"HIGHLIGHT_FONT_COLOR",                             -1},
    {"RED_FONT_COLOR",                                   -59111},
    {"DIM_RED_FONT_COLOR",                               -3401447},
    {"DULL_RED_FONT_COLOR",                              -4250074},
    {"BLUE_FONT_COLOR",                                  -16728077},
    {"GREEN_FONT_COLOR",                                 -15073511},
    {"GRAY_FONT_COLOR",                                  -8355712},
    {"YELLOW_FONT_COLOR",                                -256},
    {"LIGHTYELLOW_FONT_COLOR",                           -103},
    {"DARKYELLOW_FONT_COLOR",                            -12032},
    {"ORANGE_FONT_COLOR",                                -32704},
    {"PASSIVE_SPELL_FONT_COLOR",                         -3890432},
    {"BATTLENET_FONT_COLOR",                             -8206849},
    {"TRANSMOGRIFY_FONT_COLOR",                          -32513},
    {"DISABLED_FONT_COLOR",                              -8355712},
    {"WARNING_FONT_COLOR",                               -47104},
    {"BRIGHTBLUE_FONT_COLOR",                            -10044417},
    {"LIGHTBLUE_FONT_COLOR",                             -7885825},
    {"LIGHTGRAY_FONT_COLOR",                             -6710887},
    {"GOLD_FONT_COLOR",                                  -858471},
    {"PAPER_FRAME_EXPANDED_COLOR",                       -1193570},
    {"PAPER_FRAME_COLLAPSED_COLOR",                      -2905994},
    {"PAPER_FRAME_DARK_COLOR",                           -12573179},
    {"PAPER_FRAME_TITLE_COLOR",                          -13303808},
    {"PAPER_FRAME_TEXT_COLOR",                           -13303808},
    {"INVALID_EQUIPMENT_COLOR",                          -6815744},
    {"ACTIONBAR_HOTKEY_FONT_COLOR",                      -3355444},
    {"ARTIFACT_BAR_COLOR",                               -1651559},
    {"WARBOARD_OPTION_TEXT_COLOR",                       -12122875},
    {"DEFAULT_CHAT_CHANNEL_COLOR",                       -16192},
    {"DIM_GREEN_FONT_COLOR",                             -12533696},
    {"BLACK_FONT_COLOR",                                 -16777216},
    {"LINK_FONT_COLOR",                                  -10044417},
    {"SEPIA_COLOR",                                      -7315416},
    {"HIGHLIGHT_LIGHT_BLUE",                             -12676609},
    {"CORRUPTION_COLOR",                                 -6984239},
    {"LORE_TEXT_BODY_COLOR",                             -3488067},
    {"RARE_MISSION_COLOR",                               -16776374},
    {"TUTORIAL_FONT_COLOR",                              -3355444},
    {"DARKGRAY_COLOR",                                   -10066330},
    {"SCENARIO_STAGE_COLOR",                             16771502},
    {"SCENARIO_SUBTITLE_COLOR",                          -11167},
    {"CHALLENGE_MODE_TOAST_TITLE_COLOR",                 -2175844},
    {"TRADESKILL_EXPERIENCE_COLOR",                      -10813663},
    {"SUBSCRIPTION_INTERSTITIAL_COLOR",                  -2501170},
    {"GLUE_DIALOG_FONT_COLOR",                           -13303808},
    {"NEW_FEATURE_SHADOW_COLOR",                         -11370241},
    {"QUEST_OBJECTIVE_FONT_COLOR",                       -3355444},
    {"QUEST_OBJECTIVE_HIGHLIGHT_FONT_COLOR",             -1},
    {"QUEST_OBJECTIVE_DISABLED_FONT_COLOR",              -8421505},
    {"QUEST_OBJECTIVE_DISABLED_HIGHLIGHT_FONT_COLOR",    -6710887},
    {"AREA_NAME_FONT_COLOR",                             -5438},
    {"FACTION_RED_COLOR",                                -3388360},
    {"FACTION_ORANGE_COLOR",                             -4242176},
    {"FACTION_YELLOW_COLOR",                             -1658112},
    {"FACTION_GREEN_COLOR",                              -16738023},
    {"FRIENDS_BNET_NAME_COLOR",                          -8206849},
    {"FRIENDS_BNET_BACKGROUND_COLOR",                    201377264},
    {"FRIENDS_WOW_NAME_COLOR",                           -73380},
    {"FRIENDS_WOW_BACKGROUND_COLOR",                     218092032},
    {"FRIENDS_GRAY_COLOR",                               -8616822},
    {"FRIENDS_OFFLINE_BACKGROUND_COLOR",                 210866577},
    {"COMMON_GRAY_COLOR",                                -5723992},
    {"UNCOMMON_GREEN_COLOR",                             -15355136},
    {"RARE_BLUE_COLOR",                                  -16739854},
    {"EPIC_PURPLE_COLOR",                                -3652102},
    {"LEGENDARY_ORANGE_COLOR",                           -32768},
    {"ARTIFACT_GOLD_COLOR",                              -1651584},
    {"HEIRLOOM_BLUE_COLOR",                              -16724737},
    {"PLAYER_FACTION_COLOR_HORDE",                       -1701104},
    {"PLAYER_FACTION_COLOR_ALLIANCE",                    -11905816},
    {"TOOLTIP_DEFAULT_COLOR",                            -1},
    {"TOOLTIP_DEFAULT_BACKGROUND_COLOR",                 -15263952},
    {"KYRIAN_BLUE_COLOR",                                -13982977},
    {"VENTHYR_RED_COLOR",                                -1831666},
    {"NIGHT_FAE_BLUE_COLOR",                             -8342019},
    {"NECROLORD_GREEN_COLOR",                            -15218588},
    {"ADVENTURES_HEALING_GREEN",                         -16711918},
    {"ADVENTURES_BUFF_BLUE",                             -16721665},
    {"ADVENTURES_COMBAT_LOG_GREY",                       -4605511},
    {"ADVENTURES_COMBAT_LOG_BLUE",                       -11620905},
    {"ADVENTURES_COMBAT_LOG_ORANGE",                     -3775413},
    {"ADVENTURES_COMBAT_LOG_YELLOW",                     -11776},
    {"ENCOUNTER_JOURNAL_SCROLL_BAR_BACKGROUND_COLOR",    1073684756},
    {"RUNEFORGE_LEGEDARY_SPEC_COLOR",                    -4747414},
    {"SOULBIND_CONDUIT_ENHANCED_COLOR",                  -16724737},
    {"VERY_DARK_GRAY_COLOR",                             -14277082},
    {"VERY_LIGHT_GRAY_COLOR",                            -1710619},
    {"PURE_GREEN_COLOR",                                 -16711936},
    {"PURE_RED_COLOR",                                   -65536},
    {"TRIVIAL_DIFFICULTY_COLOR",                         -8355712},
    {"EASY_DIFFICULTY_COLOR",                            -12533696},
    {"FAIR_DIFFICULTY_COLOR",                            -256},
    {"DIFFICULT_DIFFICULTY_COLOR",                       -32704},
    {"IMPOSSIBLE_DIFFICULTY_COLOR",                      -57312},
    {"ACHIEVEMENT_INCOMPLETE_COLOR",                     -256},
    {"ACHIEVEMENT_COMPLETE_COLOR",                       -32704},
    {"NOT_ON_THREAT_COLOR",                              -1},
    {"NO_THREAT_COLOR",                                  -5197648},
    {"YELLOW_THREAT_COLOR",                              -137},
    {"ORANGE_THREAT_COLOR",                              -26368},
    {"RED_THREAT_COLOR",                                 -65536},
    {"ITEM_POOR_COLOR",                                  -6447715},
    {"ITEM_STANDARD_COLOR",                              -1},
    {"ITEM_GOOD_COLOR",                                  -14745856},
    {"ITEM_WOW_TOKEN_COLOR",                             -16724737},
    {"ITEM_EPIC_COLOR",                                  -6081042},
    {"ITEM_LEGENDARY_COLOR",                             -32768},
    {"ITEM_ARTIFACT_COLOR",                              -1651584},
    {"ITEM_SCALING_STAT_COLOR",                          -16724737},
    {"ITEM_SUPERIOR_COLOR",                              -16748323},
    {"ERROR_COLOR",                                      -57312},
    {"CONTEXT_FEEDBACK_COLOR",                           -7820545},
    {"FRIENDLY_STATUS_COLOR",                            -16711936},
    {"NEUTRAL_STATUS_COLOR",                             -256},
    {"HOSTILE_STATUS_COLOR",                             -65536},
    {"SET_ITEM_COLOR",                                   -105},
    {"INCREASE_STAT_COLOR",                              -16711936},
    {"DECREASE_STAT_COLOR",                              -57312},
    {"GLYPH_LINK_COLOR",                                 -10044417},
    {"TOY_LINK_COLOR",                                   -10044417},
    {"AZERITE_ESSENCE_COLOR",                            -10044417},
    {"SPELL_LINK_COLOR",                                 -9316865},
    {"EJ_SPELL_COLOR",                                   -14067245},
    {"ENCHANT_COLOR",                                    -12288},
    {"TALENT_LINK_COLOR",                                -11626761},
    {"INSTANCE_LOCK_LINK_COLOR",                         -32768},
    {"JOURNAL_LINK_COLOR",                               -10044417},
    {"BATTLEPET_ABILITY_LINK_COLOR",                     -11626761},
    {"ENHANCED_CONDUIT_COLOR",                           -16724737},
    {"SPELL_SUBTEXT_COLOR",                              -10931456},
    {"TRANSMOGRIFY_COLOR",                               -32513},
    {"BIND_TRADE_TOOLTIP_COLOR",                         -16724737},
    {"USED_IN_TRADESKILL_COLOR",                         -10044417},
    {"REFUND_TOOLTIP_COLOR",                             -16724737},
    {"AZERITE_SUBTEXT_COLOR",                            -10044417},
    {"FRAMESTACK_FRAME_COLOR",                           -10053121},
    {"FRAMESTACK_HIDDEN_COLOR",                          -10066330},
    {"FRAMESTACK_REGION_COLOR",                          -7807523},
    {"ACHIEVEMENT_COLOR",                                -256},
    {"FRIENDS_BROADCAST_TIME_COLOR",                     -12353112},
    {"FRIENDS_OTHER_NAME_COLOR",                         -8682359},
    {"DEFAULT_MATERIAL_TEXT_COLOR",                      -13754609},
    {"DEFAULT_MATERIAL_TITLETEXT_COLOR",                 -16777216},
    {"STONE_MATERIAL_TITLETEXT_COLOR",                   -1191680},
    {"STONE_MATERIAL_TEXT_COLOR",                        -1},
    {"PARCHMENT_MATERIAL_TEXT_COLOR",                    -13754609},
    {"PARCHMENT_MATERIAL_TITLETEXT_COLOR",               -16777216},
    {"MARBLE_MATERIAL_TEXT_COLOR",                       -16777216},
    {"MARBLE_MATERIAL_TITLETEXT_COLOR",                  -1191680},
    {"SILVER_MATERIAL_TITLETEXT_COLOR",                  -1191680},
    {"BRONZE_MATERIAL_TITLETEXT_COLOR",                  -1191680},
    {"SILVER_MATERIAL_TEXT_COLOR",                       -14737633},
    {"BRONZE_MATERIAL_TEXT_COLOR",                       -13754609},
    {"PARCHMENTLARGE_MATERIAL_TEXT_COLOR",               -14417920},
    {"PARCHMENTLARGE_MATERIAL_TITLETEXT_COLOR",          -13303808},
    {"PROGENITOR_MATERIAL_TEXT_COLOR",                   -1},
    {"PROGENITOR_MATERIAL_TITLETEXT_COLOR",              -1},
    {"ARENA_NAME_FONT_COLOR",                            -4670},
    {"AREA_DESCRIPTION_FONT_COLOR",                      -1},
    {"INVASION_FONT_COLOR",                              -3670272},
    {"INVASION_DESCRIPTION_FONT_COLOR",                  -2039},
    {"INACTIVE_COLOR",                                   -8355712},
    {"EDIT_MODE_LAYOUT_LINK_COLOR",                      -10044417},
    {"LIGHTGREEN_FONT_COLOR",                            -10027168},
    {"EDIT_MODE_GRID_LINE_COLOR",                        1073741823},
    {"EDIT_MODE_GRID_CENTER_LINE_COLOR",                 2143831546},
    {"PANEL_BACKGROUND_COLOR",                           -870375903},
    {"HEALTHBAR_MY_HEAL_PREDICTION_COLOR",               -16723005},
    {"HEALTHBAR_OTHER_HEAL_PREDICTION_COLOR",            -16735858},
    {"CINEMATIC_SUBTITLES_BLACK_BACKGROUND_COLOR",       -16777216},
    {"CINEMATIC_SUBTITLES_LIGHT_BACKGROUND_COLOR",       -1},
    {"HEALTHBAR_MY_HEAL_PREDICTION_GRADIENT_COLOR1",     -16229048},
    {"HEALTHBAR_MY_HEAL_PREDICTION_GRADIENT_COLOR2",     -16021399},
    {"HEALTHBAR_OTHER_HEAL_PREDICTION_GRADIENT_COLOR1",  -16042709},
    {"HEALTHBAR_OTHER_HEAL_PREDICTION_GRADIENT_COLOR2",  -15378104},
    {"DEBUFF_TYPE_MAGIC_COLOR",                          -16743937},
    {"DEBUFF_TYPE_POISON_COLOR",                         -8665344},
    {"DEBUFF_TYPE_CURSE_COLOR",                          -6355228},
    {"DEBUFF_TYPE_BLEED_COLOR",                          -4718577},
    {"DEBUFF_TYPE_DISEASE_COLOR",                        -955895},
    {"DEBUFF_TYPE_NONE_COLOR",                           -3407872},
    // ClassicAPI extension carried forward — Enrage was dropped from
    // GlobalColor.dbc in BC Classic. We re-add it so
    // `GetAuraDispelTypeColor("Enrage")` resolves to a proper Color
    // table via the addon-side pipeline. rgb(1.00, 0.55, 0.00) → 0xFFFF8C00.
    {"DEBUFF_TYPE_ENRAGE_COLOR",                         -29696},
    {"VISITABLE_URL_DEFAULT_CHAT_LINK_COLOR",            -16733697},
    {"EVENTTRACE_SECRET_COLOR",                          -7798904},
};

constexpr int kColorCount = sizeof(kColors) / sizeof(kColors[0]);

} // namespace UI::ColorData
