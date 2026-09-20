/*
    This file is part of Cathook.
    Cathook is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.
    Cathook is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.
    You should have received a copy of the GNU General Public License
    along with Cathook. If not, see <https://www.gnu.org/licenses/>.
*/

// Codeowners: TotallyNotElite

#include "common.hpp"
#include "micropather.h"
#include "CNavFile.h"
#include "teamroundtimer.hpp"
#include "Aimbot.hpp"
#include "MiscAimbot.hpp"
#include "NavBot.hpp"
#include "navparser.hpp"
#if ENABLE_VISUALS
#include "drawing.hpp"
#endif

#include <memory>
#include <boost/container_hash/hash.hpp>

namespace navparser
{
static settings::Boolean enabled("nav.enabled", "false");
static settings::Boolean draw("nav.draw", "false");
static settings::Boolean look{ "nav.look-at-path", "false" };
static settings::Boolean draw_debug_areas("nav.draw.debug-areas", "false");
static settings::Boolean log_pathing{ "nav.log", "false" };
static settings::Int stuck_time{ "nav.stuck-time", "800" };
static settings::Int vischeck_cache_time{ "nav.vischeck-cache.time", "240" };
static settings::Boolean vischeck_runtime{ "nav.vischeck-runtime.enabled", "true" };
static settings::Int vischeck_time{ "nav.vischeck-runtime.delay", "2000" };
static settings::Int stuck_detect_time{ "nav.anti-stuck.detection-time", "5" };
// How long until accumulated "Stuck time" expires
static settings::Int stuck_expire_time{ "nav.anti-stuck.expire-time", "10" };
// How long we should blacklist the node after being stuck for too long?
static settings::Int stuck_blacklist_time{ "nav.anti-stuck.blacklist-time", "120" };
static settings::Int sticky_ignore_time{ "nav.ignore.sticky-time", "15" };
static settings::Boolean path_during_setup{ "nav.path-during-setup", "false" };

// Cast a Ray and return if it hit
static bool CastRay(Vector origin, Vector endpos, unsigned mask, ITraceFilter *filter)
{
    trace_t trace;
    Ray_t ray;

    ray.Init(origin, endpos);

    // This was found to be So inefficient that it is literally unusable for our purposes. it is almost 1000x slower than the above.
    // ray.Init(origin, target, -right * HALF_PLAYER_WIDTH, right * HALF_PLAYER_WIDTH);

    PROF_SECTION(IEVV_TraceRay);
    g_ITrace->TraceRay(ray, mask, filter, &trace);

    return trace.DidHit();
}

// Vischeck that considers player width
static bool IsPlayerPassableNavigation(Vector origin, Vector target, unsigned int mask = MASK_PLAYERSOLID)
{
    Vector delta = target - origin;
    delta.z    = 0.0f;
    if (delta.Length() < 16.0f)
        return true;

    if (std::fabs(target.z - origin.z) <= PLAYER_JUMP_HEIGHT)
        target.z = origin.z;

    Vector right(-delta.y, delta.x, 0.0f);
    right.NormalizeInPlace();
    Vector offset = right * HALF_PLAYER_WIDTH;

    // Left ray hit something
    if (CastRay(origin - offset, target - offset, mask, &trace::filter_navigation))
        return false;

    // Return if the right ray hit something
    return !CastRay(origin + offset, target + offset, mask, &trace::filter_navigation);
}

enum class NavState
{
    Unavailable = 0,
    Active
};

struct CachedConnection
{
    int expire_tick;
    bool vischeck_state;
};

struct CachedStucktime
{
    int expire_tick;
    int time_stuck;
};

struct ConnectionInfo
{
    enum State
    {
        // Tried using this connection, failed for some reason
        STUCK,
    };
    int expire_tick;
    State state;
};

// Returns corrected "current_pos"
Vector handleDropdown(Vector current_pos, Vector next_pos)
{
    Vector to_target = (next_pos - current_pos);
    // Only do it if we'd fall quite a bit
    if (-to_target.z > PLAYER_JUMP_HEIGHT)
    {
        to_target.z = 0;
        to_target.NormalizeInPlace();
        Vector angles;
        VectorAngles(to_target, angles);
        // We need to really make sure we fall, so we go two times as far out as we should have to
        current_pos = GetForwardVector(current_pos, angles, PLAYER_WIDTH * 2.0f);
    }
    return current_pos;
}

class navPoints
{
public:
    Vector current;
    Vector center;
    // The above but on the "next" vector, used for height checks.
    Vector center_next;
    Vector next;
    navPoints(Vector A, Vector B, Vector C, Vector D) : current(A), center(B), center_next(C), next(D){};
};

// This function ensures that vischeck and pathing use the same logic.
navPoints determinePoints(CNavArea *current, CNavArea *next)
{
    auto area_center = current->m_center;
    auto next_center = next->m_center;
    // Gets a vector on the edge of the current area that is as close as possible to the center of the next area
    auto area_closest = current->getNearestPoint(next_center.AsVector2D());
    // Do the same for the other area
    auto next_closest = next->getNearestPoint(area_center.AsVector2D());

    // Use one of them as a center point, the one that is either x or y alligned with a center
    // Of the areas.
    // This will avoid walking into walls.
    auto center_point = area_closest;

    // Determine if alligned, if not, use the other one as the center point
    if (center_point.x != area_center.x && center_point.y != area_center.y && center_point.x != next_center.x && center_point.y != next_center.y)
    {
        center_point = next_closest;
        // Use the point closest to next_closest on the "original" mesh for z
        center_point.z = current->getNearestPoint(next_closest.AsVector2D()).z;
    }

    // Nearest point to center on "next"m used for height checks
    auto center_next = next->getNearestPoint(center_point.AsVector2D());

    return navPoints(area_center, center_point, center_next, next_center);
};

class Map : public micropather::Graph
{
public:
    CNavFile navfile;
    NavState state;
    micropather::MicroPather pather{ this, 3000, 6, true };
    std::string mapname;
    std::unordered_map<std::pair<CNavArea *, CNavArea *>, CachedConnection, boost::hash<std::pair<CNavArea *, CNavArea *>>> vischeck_cache;
    std::unordered_map<std::pair<CNavArea *, CNavArea *>, CachedStucktime, boost::hash<std::pair<CNavArea *, CNavArea *>>> connection_stuck_time;
    // This is a pure blacklist that does not get cleared and is for free usage internally and externally, e.g. blacklisting where enemies are standing
    // This blacklist only gets cleared on map change, and can be used time independantly.
    // the enum is the Blacklist reason, so you can easily edit it
    std::unordered_map<CNavArea *, BlacklistReason> free_blacklist;
    // When the local player stands on one of the nav squares the free blacklist should NOT run
    bool free_blacklist_blocked = false;

    CNavArea *solve_start = nullptr;
    CNavArea *solve_dest  = nullptr;
    bool solve_in_setup   = false;

    Map(const char *nav_path, std::string level_name) : navfile(nav_path), mapname(std::move(level_name))
    {
        if (!navfile.m_isOK)
            state = NavState::Unavailable;
        else
            state = NavState::Active;
    }
    float LeastCostEstimate(void *start, void *end) override
    {
        return reinterpret_cast<CNavArea *>(start)->m_center.DistTo(reinterpret_cast<CNavArea *>(end)->m_center);
    }
    void AdjacentCost(void *main, std::vector<micropather::StateCost> *adjacent) override
    {
        CNavArea &area = *reinterpret_cast<CNavArea *>(main);
        int team       = g_pLocalPlayer->team;
        for (NavConnect &connection : area.m_connections)
        {
            CNavArea *next_area = connection.area;
            if (!next_area)
                continue;

            if (next_area->m_minZ - area.m_maxZ > PLAYER_JUMP_HEIGHT)
                continue;
            if (area.m_minZ - next_area->m_maxZ > PLAYER_DEATH_DROP_HEIGHT)
                continue;

            int tf_attributes = next_area->m_TFattributeFlags;
            if ((tf_attributes & TF_NAV_BLOCKED) && !(tf_attributes & TF_NAV_UNBLOCKABLE))
                continue;

            if (solve_in_setup && next_area != solve_dest && (tf_attributes & (TF_NAV_BLUE_SETUP_GATE | TF_NAV_RED_SETUP_GATE)))
                continue;

            if (team == TEAM_RED && (tf_attributes & (TF_NAV_SPAWN_ROOM_BLUE | TF_NAV_BLUE_ONE_WAY_DOOR)))
                continue;
            if (team == TEAM_BLU && (tf_attributes & (TF_NAV_SPAWN_ROOM_RED | TF_NAV_RED_ONE_WAY_DOOR)))
                continue;

            // An area being entered twice means it is blacklisted from entry entirely
            auto connection_key    = std::pair<CNavArea *, CNavArea *>(next_area, next_area);
            auto cached_connection = vischeck_cache.find(connection_key);

            // Entered and marked bad?
            if (cached_connection != vischeck_cache.end())
                if (!cached_connection->second.vischeck_state)
                    continue;

            // If the extern blacklist is running, ensure we don't try to use a bad area
            bool is_blacklisted = false;
            if (!free_blacklist_blocked)
                for (auto const &entry : free_blacklist)
                {
                    if (entry.first == next_area)
                    {
                        is_blacklisted = true;
                        break;
                    }
                }
            if (is_blacklisted)
                continue;

            auto points = determinePoints(&area, next_area);

            // Apply dropdown
            points.center = handleDropdown(points.center, points.next);

            float height_diff = points.center_next.z - points.center.z;

            // Too high for us to jump!
            if (height_diff > PLAYER_JUMP_HEIGHT)
                continue;

            float cost_multiplier = 1.0f;
            if (next_area->m_attributeFlags & NAV_MESH_AVOID)
                cost_multiplier += 3.0f;
            if (team == TEAM_RED && (tf_attributes & TF_NAV_BLUE_SENTRY_DANGER))
                cost_multiplier += 1.0f;
            else if (team == TEAM_BLU && (tf_attributes & TF_NAV_RED_SENTRY_DANGER))
                cost_multiplier += 1.0f;

            points.current.z += PLAYER_JUMP_HEIGHT;
            points.center.z += PLAYER_JUMP_HEIGHT;
            points.next.z += PLAYER_JUMP_HEIGHT;

            auto key    = std::pair<CNavArea *, CNavArea *>(&area, next_area);
            auto cached = vischeck_cache.find(key);
            if (cached != vischeck_cache.end())
            {
                if (cached->second.vischeck_state)
                {
                    float cost = next_area->m_center.DistTo(area.m_center) * cost_multiplier;
                    adjacent->push_back(micropather::StateCost{ reinterpret_cast<void *>(next_area), cost });
                }
            }
            else
            {
                // Check if there is direct line of sight
                if (IsPlayerPassableNavigation(points.current, points.center) && IsPlayerPassableNavigation(points.center, points.next))
                {
                    vischeck_cache[key] = { TICKCOUNT_TIMESTAMP(60), true };

                    float cost = points.next.DistTo(points.current) * cost_multiplier;
                    adjacent->push_back(micropather::StateCost{ reinterpret_cast<void *>(next_area), cost });
                }
                else
                {
                    vischeck_cache[key] = { TICKCOUNT_TIMESTAMP(60), false };
                }
            }
        }
    }

    // Function for getting closest Area to player, aka "LocalNav"
    CNavArea *findClosestNavSquare(const Vector &vec)
    {
        float best_score      = FLT_MAX;
        CNavArea *best_area   = nullptr;
        bool is_local         = g_pLocalPlayer->v_Origin == vec;
        float overlap_padding = is_local ? HALF_PLAYER_WIDTH : 0.0f;

        for (auto &i : navfile.m_areas)
        {
            // Marked bad, do not use if local origin
            if (is_local)
            {
                auto key   = std::pair<CNavArea *, CNavArea *>(&i, &i);
                auto found = vischeck_cache.find(key);
                if (found != vischeck_cache.end() && !found->second.vischeck_state)
                    continue;
            }

            float nearest_x = std::clamp(vec.x, i.m_nwCorner.x, i.m_seCorner.x);
            float nearest_y = std::clamp(vec.y, i.m_nwCorner.y, i.m_seCorner.y);
            float surface_z = i.GetZ(nearest_x, nearest_y);

            float dx              = nearest_x - vec.x;
            float dy              = nearest_y - vec.y;
            float planar_dist_sqr = dx * dx + dy * dy;

            float vertical_to_surface = std::fabs(surface_z - vec.z);
            float vertical_outside    = std::max(i.m_minZ - vec.z, 0.0f) + std::max(vec.z - i.m_maxZ, 0.0f);

            float score = planar_dist_sqr + vertical_to_surface * vertical_to_surface * 6.0f + vertical_outside * vertical_outside * 10.0f;
            if (i.IsOverlapping(vec, overlap_padding))
                score *= is_local ? 0.45f : 0.7f;

            if (is_local)
            {
                float delta = vec.z - surface_z;
                if (delta < -18.0f)
                    score += delta * delta * 28.0f;
                else if (delta < -6.0f)
                    score += delta * delta * 10.0f;
            }

            if (score < best_score)
            {
                best_score = score;
                best_area  = &i;
            }
        }
        return best_area;
    }
    std::vector<void *> findPath(CNavArea *local, CNavArea *dest, bool in_setup)
    {
        using namespace std::chrono;

        if (state != NavState::Active)
            return {};

        solve_start     = local;
        solve_dest      = dest;
        solve_in_setup  = in_setup;

        if (log_pathing)
        {
            logging::Info("Start: (%f,%f,%f)", local->m_center.x, local->m_center.y, local->m_center.z);
            logging::Info("End: (%f,%f,%f)", dest->m_center.x, dest->m_center.y, dest->m_center.z);
        }

        std::vector<void *> pathNodes;
        float cost;

        time_point begin_pathing = high_resolution_clock::now();
        int result               = pather.Solve(reinterpret_cast<void *>(local), reinterpret_cast<void *>(dest), &pathNodes, &cost);
        long long timetaken      = duration_cast<nanoseconds>(high_resolution_clock::now() - begin_pathing).count();
        if (log_pathing)
            logging::Info("Pathing: Pather result: %i. Time taken (NS): %lld", result, timetaken);
        // Start and end are the same, return start node
        if (result == micropather::MicroPather::START_END_SAME)
            return { reinterpret_cast<void *>(local) };

        return pathNodes;
    }

    void updateIgnores()
    {
        static Timer update_time;
        if (!update_time.test_and_set(1000))
            return;

        // Sentries make sounds, so we can just rely on soundcache here and always clear sentries
        NavEngine::clearFreeBlacklist(SENTRY);
        // Find sentries and stickies
        for (int i = g_IEngine->GetMaxClients() + 1; i < MAX_ENTITIES; i++)
        {
            CachedEntity* ent = ENTITY(i);
            if (CE_INVALID(ent) || !ent->m_bAlivePlayer() || ent->m_iTeam() == g_pLocalPlayer->team)
                continue;
            bool is_sentry = ent->m_iClassID() == CL_CLASS(CObjectSentrygun);
            bool is_sticky = ent->m_iClassID() == CL_CLASS(CTFGrenadePipebombProjectile) && CE_INT(ent, netvar.iPipeType) == 1 && CE_VECTOR(ent, netvar.vVelocity).IsZero(1.0f);
            // Not sticky/sentry, ignore.
            // (Or dormant sticky)
            if (!is_sentry && (!is_sticky || CE_BAD(ent)))
                continue;
            if (is_sentry)
            {
                if (CE_INT(ent, netvar.m_iSentryState) == 0)
                    continue;

                // Should we even ignore the sentry?
                // Soldier/Heavy do not care about Level 1 or mini sentries
                bool is_strong_class = g_pLocalPlayer->clazz == tf_soldier || g_pLocalPlayer->clazz == tf_heavy;
                int bullet           = CE_INT(ent, netvar.m_iAmmoShells);
                int rocket           = CE_INT(ent, netvar.m_iAmmoRockets);
                if ((is_strong_class && (CE_BYTE(ent, netvar.m_bMiniBuilding) || CE_INT(ent, netvar.iUpgradeLevel) == 1)) || (bullet == 0 && (CE_INT(ent, netvar.iUpgradeLevel) != 3 || rocket == 0)))
                    continue;

                // It's still building/being sapped, ignore.
                // Unless it just was deployed from a carry, then it's dangerous
                if ((!CE_BYTE(ent, netvar.m_bCarryDeploy) && CE_BYTE(ent, netvar.m_bBuilding)) || CE_BYTE(ent, netvar.m_bPlacing) || CE_BYTE(ent, netvar.m_bHasSapper))
                    continue;

                // Get origin of the sentry
                auto building_origin = GetBuildingPosition(ent);
                // For dormant sentries we need to add the jump height to the z
                if (CE_BAD(ent))
                    building_origin.z += PLAYER_JUMP_HEIGHT;
                // Actual building check
                for (auto &i : navfile.m_areas)
                {
                    Vector area = i.m_center;
                    area.z += PLAYER_JUMP_HEIGHT;
                    // Out of range
                    if (building_origin.DistToSqr(area) > (1100 + HALF_PLAYER_WIDTH) * (1100 + HALF_PLAYER_WIDTH))
                        continue;
                    // Check if sentry can see us
                    if (!IsVectorVisibleNavigation(building_origin, area))
                        continue;
                    // Blacklist because it's in view range of the sentry
                    free_blacklist[&i] = SENTRY;
                }
            }
            else
            {
                auto sticky_origin = ent->m_vecOrigin();
                // Make sure the sticky doesn't vischeck from inside the floor
                sticky_origin.z += PLAYER_JUMP_HEIGHT / 2.0f;
                for (auto &i : navfile.m_areas)
                {
                    Vector area = i.m_center;
                    area.z += PLAYER_JUMP_HEIGHT;
                    // Out of range
                    if (sticky_origin.DistToSqr(area) > (130 + HALF_PLAYER_WIDTH) * (130 + HALF_PLAYER_WIDTH))
                        continue;
                    // Check if Sticky can see the reason
                    if (!IsVectorVisibleNavigation(sticky_origin, area))
                        continue;
                    // Blacklist because it's in range of the sticky, but stickies make no noise, so blacklist it for a specific timeframe
                    free_blacklist[&i] = { STICKY, TICKCOUNT_TIMESTAMP(*sticky_ignore_time) };
                }
            }
        }

        static size_t previous_blacklist_size = 0;

        bool erased = false;
        if (previous_blacklist_size != free_blacklist.size())
            erased = true;
        previous_blacklist_size = free_blacklist.size();
        // When we switch to c++20, we can use std::erase_if
        for (auto it = begin(free_blacklist); it != end(free_blacklist);)
        {
            // Clear entries from the free blacklist when expired and if it has a set time
            if (it->second.time && it->second.time < g_GlobalVars->tickcount)
            {
                it     = free_blacklist.erase(it); // previously this was something like m_map.erase(it++);
                erased = true;
            }
            else
                ++it;
        }

        for (auto it = begin(vischeck_cache); it != end(vischeck_cache);)
        {
            if (it->second.expire_tick < g_GlobalVars->tickcount)
            {
                it     = vischeck_cache.erase(it); // previously this was something like m_map.erase(it++);
                erased = true;
            }
            else
                ++it;
        }
        for (auto it = begin(connection_stuck_time); it != end(connection_stuck_time);)
        {
            if (it->second.expire_tick < g_GlobalVars->tickcount)
            {
                it     = connection_stuck_time.erase(it); // previously this was something like m_map.erase(it++);
                erased = true;
            }
            else
                ++it;
        }
        if (erased)
            pather.Reset();
    }

    void Reset()
    {
        vischeck_cache.clear();
        connection_stuck_time.clear();
        free_blacklist.clear();
        pather.Reset();
    }

    // Uncesseray thing that is sadly necessary
    void PrintStateInfo(void *) override
    {
    }
};

namespace NavEngine
{
std::unique_ptr<Map> map;
Crumb last_crumb;
std::vector<Crumb> crumbs;

int current_priority    = 0;
bool current_navtolocal = false;
bool repath_on_fail     = false;
Vector last_destination;
Vector failed_destination;
int failed_dest_expire_tick = 0;
static bool map_dirty = true;

static bool isSetupTime()
{
    if (path_during_setup)
        return false;
    if (GetLevelName() == "plr_pipeline")
        return false;
    if (g_pGameRules->InSetup() || g_pGameRules->RoundMode() == CGameRules::GR_STATE_PREROUND)
        return true;
    return g_pTeamRoundTimer->GetRoundState() == RT_STATE_SETUP;
}

static bool isPathingBlocked()
{
    if (GetLevelName() == "plr_pipeline")
        return false;
    std::string level_name = GetLevelName();
    if (g_pGameRules->InWaitingForPlayers() && (level_name.starts_with("pl_") || level_name.starts_with("cp_")))
        return true;
    if (isSetupTime() && g_pLocalPlayer->team == TEAM_BLU)
        return true;
    return false;
}

static std::string resolveNavPath(const std::string &level_name)
{
    std::vector<std::string> candidates;
    const char *game_dir = g_IEngine->GetGameDirectory();
    if (game_dir)
    {
        candidates.emplace_back(std::string(game_dir) + "/maps/" + level_name + ".nav");
        candidates.emplace_back(std::string(game_dir) + "/download/maps/" + level_name + ".nav");
    }
    char cwd[PATH_MAX + 1];
    if (getcwd(cwd, sizeof(cwd)))
        candidates.emplace_back(std::string(cwd) + "/tf/maps/" + level_name + ".nav");

    for (auto &candidate : candidates)
    {
        std::ifstream fs(candidate, std::ios::binary);
        if (fs.is_open())
            return candidate;
    }
    return candidates.empty() ? "" : candidates.front();
}

void cancelPath();

static void ensureMapLoaded()
{
    if (!map_dirty && map)
        return;
    if (!g_IEngine->IsInGame())
        return;
    std::string level_name = GetLevelName();
    if (level_name.empty())
        return;
    if (map && map->mapname == level_name)
    {
        map->Reset();
        map_dirty = false;
        return;
    }

    crumbs.clear();
    last_crumb.navarea   = nullptr;
    current_priority     = 0;
    repath_on_fail       = false;
    failed_dest_expire_tick = 0;

    std::string nav_path = resolveNavPath(level_name);
    logging::Info("Pathing: Nav File location: %s", nav_path.c_str());
    map       = std::make_unique<Map>(nav_path.c_str(), level_name);
    map_dirty = false;
}

bool isReady()
{
    ensureMapLoaded();
    if (!enabled && !hacks::tf2::NavBot::isEnabled())
        return false;
    if (!map || map->state != NavState::Active || !g_IEngine->IsInGame())
        return false;
    return !isPathingBlocked();
}

bool isPathing()
{
    return !crumbs.empty();
}

CNavFile *getNavFile()
{
    return &map->navfile;
}

CNavArea *findClosestNavSquare(const Vector origin)
{
    return map->findClosestNavSquare(origin);
}

std::vector<Crumb> *getCrumbs()
{
    return &crumbs;
}

static Timer inactivity{};
static Timer time_spent_on_crumb{};

bool navTo(const Vector &destination, int priority, bool should_repath, bool nav_to_local, bool is_repath)
{
    if (!isReady())
        return false;
    // Don't path, priority is too low
    if (priority < current_priority)
        return false;

    constexpr float REUSE_RADIUS_SQR = 160.0f * 160.0f;

    if (isPathing() && priority == current_priority && repath_on_fail && last_destination.DistToSqr(destination) <= REUSE_RADIUS_SQR)
    {
        last_destination = destination;
        if (!crumbs.empty())
            crumbs.back().vec = destination;
        inactivity.update();
        return true;
    }

    if (failed_dest_expire_tick > g_GlobalVars->tickcount && failed_destination.DistToSqr(destination) <= REUSE_RADIUS_SQR)
        return false;

    if (log_pathing)
        logging::Info("Priority: %d", priority);

    CNavArea *start_area = map->findClosestNavSquare(g_pLocalPlayer->v_Origin);
    CNavArea *dest_area  = map->findClosestNavSquare(destination);

    if (!start_area || !dest_area)
        return false;
    auto path = map->findPath(start_area, dest_area, isSetupTime());
    if (path.empty())
    {
        failed_destination      = destination;
        failed_dest_expire_tick = TICKCOUNT_TIMESTAMP(1.5f);
        return false;
    }

    if (!nav_to_local)
        path.erase(path.begin());
    crumbs.clear();

    if (path.empty())
    {
        crumbs.push_back({ nullptr, destination });
        inactivity.update();
        current_priority   = priority;
        current_navtolocal = nav_to_local;
        repath_on_fail     = should_repath;
        if (repath_on_fail)
            last_destination = destination;
        return true;
    }

    constexpr float CRUMB_STEP = 128.0f;
    Vector entry               = g_pLocalPlayer->v_Origin;

    auto add_segment = [&](CNavArea *area, const Vector &to)
    {
        Vector delta = to - entry;
        delta.z      = 0.0f;
        int steps    = (int) std::ceil(delta.Length() / CRUMB_STEP);
        for (int s = 1; s < steps; ++s)
        {
            Vector pos = entry + delta * ((float) s / (float) steps);
            pos.z      = area->GetZ(pos.x, pos.y);
            crumbs.push_back({ area, pos });
        }
        crumbs.push_back({ area, to });
        entry = to;
    };

    for (size_t i = 0; i + 1 < path.size(); ++i)
    {
        CNavArea *area      = reinterpret_cast<CNavArea *>(path[i]);
        CNavArea *next_area = reinterpret_cast<CNavArea *>(path[i + 1]);

        auto points   = determinePoints(area, next_area);
        points.center = handleDropdown(points.center, points.next);

        add_segment(area, points.center);
        entry = points.center_next;
    }

    CNavArea *last_area  = reinterpret_cast<CNavArea *>(path.back());
    Vector last_waypoint = last_area->getNearestPoint(destination.AsVector2D());
    add_segment(last_area, last_waypoint);
    crumbs.push_back({ nullptr, destination });
    inactivity.update();

    current_priority   = priority;
    current_navtolocal = nav_to_local;
    repath_on_fail     = should_repath;
    // Ensure we know where to go
    if (repath_on_fail)
        last_destination = destination;

    return true;
}

// Use when something unexpected happens, e.g. vischeck fails
void abandonPath()
{
    if (!map)
        return;
    map->pather.Reset();
    crumbs.clear();
    last_crumb.navarea = nullptr;
    // We want to repath on failure
    if (repath_on_fail)
        navTo(last_destination, current_priority, true, current_navtolocal, false);
    else
        current_priority = 0;
}

// Use to cancel pathing completely
void cancelPath()
{
    crumbs.clear();
    last_crumb.navarea = nullptr;
    current_priority   = 0;
}

static Timer last_jump{};
// Used to determine if we want to jump or if we want to crouch
static bool crouch          = false;
static int ticks_since_jump = 0;
static Crumb current_crumb;

static void followCrumbs()
{
    size_t crumbs_amount = crumbs.size();

    // No more crumbs, reset status
    if (!crumbs_amount)
    {
        // Invalidate last crumb
        last_crumb.navarea = nullptr;

        repath_on_fail   = false;
        current_priority = 0;
        return;
    }

    if (current_crumb.navarea != crumbs[0].navarea)
        time_spent_on_crumb.update();
    current_crumb = crumbs[0];

    // Ensure we do not try to walk downwards unless we are falling
    static std::vector<float> fall_vec{};
    Vector vel;
    velocity::EstimateAbsVelocity(RAW_ENT(LOCAL_E), vel);

    fall_vec.push_back(vel.z);
    if (fall_vec.size() > 10)
        fall_vec.erase(fall_vec.begin());

    bool reset_z = true;
    for (auto const &entry : fall_vec)
    {
        if (!(entry <= 0.01f && entry >= -0.01f))
            reset_z = false;
    }
    if (reset_z)
    {
        reset_z = false;

        Ray_t ray;
        trace_t trace;
        Vector end = g_pLocalPlayer->v_Origin;
        end.z -= 100.0f;

        trace::filter_default.SetSelf(RAW_ENT(LOCAL_E));

        ray.Init(g_pLocalPlayer->v_Origin, end, EntOBBMins(RAW_ENT(LOCAL_E)), EntOBBMaxs(RAW_ENT(LOCAL_E)));
        g_ITrace->TraceRay(ray, MASK_PLAYERSOLID, &trace::filter_default, &trace);
        // Only reset if we are standing on a building
        if (trace.DidHit() && trace.m_pEnt && ENTITY(EntIndex((IClientEntity *) trace.m_pEnt))->m_Type() == ENTITY_BUILDING)
            reset_z = true;
    }

    Vector current_vec = crumbs[0].vec;
    if (reset_z)
        current_vec.z = g_pLocalPlayer->v_Origin.z;

    // We are close enough to the crumb to have reached it
    if (current_vec.DistTo(g_pLocalPlayer->v_Origin) < 50)
    {
        last_crumb = crumbs[0];
        crumbs.erase(crumbs.begin());
        time_spent_on_crumb.update();
        if (!--crumbs_amount)
            return;
        inactivity.update();
    }

    current_vec = crumbs[0].vec;
    if (reset_z)
        current_vec.z = g_pLocalPlayer->v_Origin.z;

    // We are close enough to the second crumb, Skip both (This is espcially helpful with drop downs)
    if (crumbs.size() > 1 && crumbs[1].vec.DistTo(g_pLocalPlayer->v_Origin) < 50)
    {
        last_crumb = crumbs[1];
        crumbs.erase(crumbs.begin(), crumbs.begin() + 2);
        if (crumbs.empty())
            return;
        inactivity.update();
    }

    // If we make any progress at all, reset this
    else
    {
        // If we spend way too long on this crumb, ignore the logic below
        if (!time_spent_on_crumb.check(*stuck_detect_time * 1000))
        {
            Vector vel;
            velocity::EstimateAbsVelocity(RAW_ENT(LOCAL_E), vel);
            // 44.0f -> Revved brass beast, do not use z axis as jumping counts towards that. Yes this will mean long falls will trigger it, but that is not really bad.
            if (!vel.AsVector2D().IsZero(40.0f))
                inactivity.update();
        }
    }

    // Detect when jumping is necessary.
    // 1. No jumping if zoomed (or revved)
    // 2. Jump if its necessary to do so based on z values
    // 3. Jump if stuck (not getting closer) for more than stuck_time/2 (500ms)
    if ((!(g_pLocalPlayer->holding_sniper_rifle && g_pLocalPlayer->bZoomed) && !(g_pLocalPlayer->bRevved || g_pLocalPlayer->bRevving) && (crouch || crumbs[0].vec.z - g_pLocalPlayer->v_Origin.z > 18) && last_jump.check(200)) || (last_jump.check(200) && inactivity.check(*stuck_time / 2)))
    {
        auto local = map->findClosestNavSquare(g_pLocalPlayer->v_Origin);
        // Check if current area allows jumping
        if (!local || !(local->m_attributeFlags & (NAV_MESH_NO_JUMP | NAV_MESH_STAIRS)))
        {
            // Make it crouch until we land, but jump the first tick
            current_user_cmd->buttons |= crouch ? IN_DUCK : IN_JUMP;

            // Only flip to crouch state, not to jump state
            if (!crouch)
            {
                crouch           = true;
                ticks_since_jump = 0;
            }
            ticks_since_jump++;

            // Update jump timer now since we are back on ground
            if (crouch && CE_INT(LOCAL_E, netvar.iFlags) & FL_ONGROUND && ticks_since_jump > 3)
            {
                // Reset
                crouch = false;
                last_jump.update();
            }
        }
    }

    /*if (inactivity.check(*stuck_time) || (inactivity.check(*unreachable_time) && !IsVectorVisible(g_pLocalPlayer->v_Origin, *crumb_vec + Vector(.0f, .0f, 41.5f), false, LOCAL_E, MASK_PLAYERSOLID)))
    {
        if (crumbs[0].navarea)
            ignoremanager::addTime(last_area, *crumb, inactivity);
        repath();
        return;
    }*/

    // Look at path
    if (look && !hacks::shared::aimbot::isAiming())
    {
        Vector next{ crumbs[0].vec.x, crumbs[0].vec.y, g_pLocalPlayer->v_Eye.z };
        next = GetAimAtAngles(g_pLocalPlayer->v_Eye, next);

        // Slow aim to smoothen
        hacks::tf2::misc_aimbot::DoSlowAim(next);
        current_user_cmd->viewangles = next;
    }

    WalkTo(current_vec);
}

static Timer vischeck_timer{};
void vischeckPath()
{
    // No crumbs to check, or vischeck timer should not run yet, bail.
    if (crumbs.size() < 2 || !vischeck_timer.test_and_set(*vischeck_time))
        return;

    // Iterate all the crumbs
    for (int i = 0; i < (int) crumbs.size() - 1; i++)
    {
        auto current_crumb  = crumbs[i];
        auto next_crumb     = crumbs[i + 1];
        if (!current_crumb.navarea || !next_crumb.navarea)
            continue;
        auto current_center = current_crumb.vec;
        auto next_center    = next_crumb.vec;

        current_center.z += PLAYER_JUMP_HEIGHT;
        next_center.z += PLAYER_JUMP_HEIGHT;
        auto key = std::pair<CNavArea *, CNavArea *>(current_crumb.navarea, next_crumb.navarea);
        // Check if we can pass, if not, abort pathing and mark as bad
        if (!IsPlayerPassableNavigation(current_center, next_center))
        {
            // Mark as invalid for a while
            map->vischeck_cache[key] = { TICKCOUNT_TIMESTAMP(*vischeck_cache_time), false };
            abandonPath();
            return;
        }
        // Else we can update the cache (if not marked bad before this)
        else if (map->vischeck_cache.find(key) == map->vischeck_cache.end() || map->vischeck_cache[key].vischeck_state)
        {
            map->vischeck_cache[key] = { TICKCOUNT_TIMESTAMP(*vischeck_cache_time), true };
        }
    }
}

static Timer blacklist_check_timer{};
// Check if one of the crumbs is suddenly blacklisted
void checkBlacklist()
{
    // Only check every 500ms
    if (!blacklist_check_timer.test_and_set(500))
        return;

    // Local player is ubered and does not care about the blacklist
    // TODO: Only for damage type things
    if (IsPlayerInvulnerable(LOCAL_E))
    {
        map->free_blacklist_blocked = true;
        map->pather.Reset();
        return;
    }
    CNavArea *local_area = map->findClosestNavSquare(g_pLocalPlayer->v_Origin);
    for (auto const &entry : map->free_blacklist)
    {
        // Local player is in a blocked area, so temporarily remove the blacklist as else we would be stuck
        if (entry.first == local_area)
        {
            map->free_blacklist_blocked = true;
            map->pather.Reset();
            return;
        }
    }

    // Local player is not blocking the nav area, so blacklist should not be marked as blocked
    map->free_blacklist_blocked = false;

    bool should_abandon = false;
    for (auto &crumb : crumbs)
    {
        if (should_abandon)
            break;
        // A path Node is blacklisted, abandon pathing
        for (auto const &entry : map->free_blacklist)
        {
            if (entry.first == crumb.navarea)
                should_abandon = true;
        }
    }
    if (should_abandon)
        abandonPath();
}

void updateStuckTime()
{
    // No crumbs
    if (!crumbs.size())
        return;
    // We're stuck, add time to connection
    if (inactivity.check(*stuck_time / 2))
    {
        CNavArea *from = last_crumb.navarea ? last_crumb.navarea : crumbs[0].navarea;
        CNavArea *to   = crumbs[0].navarea;
        if (!from || !to)
        {
            if (inactivity.check(*stuck_time))
                abandonPath();
            return;
        }
        auto key = std::pair<CNavArea *, CNavArea *>(from, to);

        // Expires in 10 seconds
        map->connection_stuck_time[key].expire_tick = TICKCOUNT_TIMESTAMP(*stuck_expire_time);
        // Stuck for one tick
        map->connection_stuck_time[key].time_stuck += 1;

        // We are stuck for too long, blastlist node for a while and repath
        if (map->connection_stuck_time[key].time_stuck > TIME_TO_TICKS(*stuck_detect_time))
        {
            map->vischeck_cache[key].expire_tick    = TICKCOUNT_TIMESTAMP(*stuck_blacklist_time);
            map->vischeck_cache[key].vischeck_state = false;
            if (log_pathing)
                logging::Info("Blackisted connection %d->%d", key.first->m_id, key.second->m_id);
            abandonPath();
        }
    }
}

static void CreateMove()
{
    ensureMapLoaded();
    if (!isReady())
        return;
    if (CE_BAD(LOCAL_E) || !LOCAL_E->m_bAlivePlayer())
    {
        cancelPath();
        return;
    }

    if (vischeck_runtime)
        vischeckPath();
    checkBlacklist();

    followCrumbs();
    updateStuckTime();
    map->updateIgnores();
}

void LevelInit()
{
    map_dirty = true;
    cancelPath();
}

// Return the whole thing
std::unordered_map<CNavArea *, BlacklistReason> *getFreeBlacklist()
{
    return &map->free_blacklist;
}

// Return a specific category, we keep the same indexes to provide single element erasing
std::unordered_map<CNavArea *, BlacklistReason> getFreeBlacklist(BlacklistReason reason)
{
    std::unordered_map<CNavArea *, BlacklistReason> return_map;
    for (auto const &entry : map->free_blacklist)
    {
        // Category matches
        if (entry.second.value == reason.value)
            return_map[entry.first] = entry.second;
    }
    return return_map;
}

// Clear whole blacklist
void clearFreeBlacklist()
{
    map->free_blacklist.clear();
}

// Clear by category
void clearFreeBlacklist(BlacklistReason reason)
{
    for (auto it = begin(map->free_blacklist); it != end(map->free_blacklist);)
    {
        if (it->second.value == reason.value)
            it = map->free_blacklist.erase(it); // previously this was something like m_map.erase(it++);
        else
            ++it;
    }
}

#if ENABLE_VISUALS
void drawNavArea(CNavArea *area)
{
    Vector nw, ne, sw, se;
    bool nw_screen = draw::WorldToScreen(area->m_nwCorner, nw);
    bool ne_screen = draw::WorldToScreen(area->getNeCorner(), ne);
    bool sw_screen = draw::WorldToScreen(area->getSwCorner(), sw);
    bool se_screen = draw::WorldToScreen(area->m_seCorner, se);

    // Nw -> Ne
    if (nw_screen && ne_screen)
        draw::Line(nw.x, nw.y, ne.x - nw.x, ne.y - nw.y, colors::green, 1.0f);
    // Nw -> Sw
    if (nw_screen && sw_screen)
        draw::Line(nw.x, nw.y, sw.x - nw.x, sw.y - nw.y, colors::green, 1.0f);
    // Ne -> Se
    if (ne_screen && se_screen)
        draw::Line(ne.x, ne.y, se.x - ne.x, se.y - ne.y, colors::green, 1.0f);
    // Sw -> Se
    if (sw_screen && se_screen)
        draw::Line(sw.x, sw.y, se.x - sw.x, se.y - sw.y, colors::green, 1.0f);
}

void Draw()
{
    if (!isReady() || !draw)
        return;
    if (draw_debug_areas && CE_GOOD(LOCAL_E) && LOCAL_E->m_bAlivePlayer())
    {
        auto area = map->findClosestNavSquare(g_pLocalPlayer->v_Origin);
        if (!area)
            return;
        auto edge = area->getNearestPoint(g_pLocalPlayer->v_Origin.AsVector2D());
        Vector scrEdge;
        edge.z += PLAYER_JUMP_HEIGHT;
        if (draw::WorldToScreen(edge, scrEdge))
            draw::Rectangle(scrEdge.x - 2.0f, scrEdge.y - 2.0f, 4.0f, 4.0f, colors::red);
        drawNavArea(area);
    }

    if (crumbs.empty())
        return;

    for (size_t i = 0; i < crumbs.size(); i++)
    {
        Vector start_pos = crumbs[i].vec;

        Vector start_screen, end_screen;
        if (draw::WorldToScreen(start_pos, start_screen))
        {
            draw::Rectangle(start_screen.x - 5.0f, start_screen.y - 5.0f, 10.0f, 10.0f, colors::white);

            if (i < crumbs.size() - 1)
            {
                Vector end_pos = crumbs[i + 1].vec;
                if (draw::WorldToScreen(end_pos, end_screen))
                    draw::Line(start_screen.x, start_screen.y, end_screen.x - start_screen.x, end_screen.y - start_screen.y, colors::white, 2.0f);
            }
        }
    }
}
#endif
}; // namespace NavEngine

Vector loc;

static CatCommand nav_set("nav_set", "Debug nav find", []() { loc = g_pLocalPlayer->v_Origin; });

static CatCommand nav_path("nav_path", "Debug nav path", []() { NavEngine::navTo(loc, 20, true, true, false); });

static CatCommand nav_path_noreapth("nav_path_norepath", "Debug nav path", []() { NavEngine::navTo(loc, 20, false, true, false); });

static CatCommand nav_init("nav_init", "Reload nav mesh",
                           []()
                           {
                               NavEngine::map.reset();
                               NavEngine::LevelInit();
                           });

static CatCommand nav_debug_check("nav_debug_check", "Perform nav checks between two areas. First area: cat_nav_set Second area: Your location while running this command.",
                                  []()
                                  {
                                      if (!NavEngine::isReady())
                                          return;
                                      auto next    = NavEngine::map->findClosestNavSquare(g_pLocalPlayer->v_Origin);
                                      auto current = NavEngine::map->findClosestNavSquare(loc);

                                      auto points = determinePoints(current, next);

                                      points.center = handleDropdown(points.center, points.next);

                                      // Too high for us to jump!
                                      if (points.center_next.z - points.center.z > PLAYER_JUMP_HEIGHT)
                                      {
                                          return logging::Info("Nav: Area too high!");
                                      }

                                      points.current.z += PLAYER_JUMP_HEIGHT;
                                      points.center.z += PLAYER_JUMP_HEIGHT;
                                      points.next.z += PLAYER_JUMP_HEIGHT;

                                      if (IsPlayerPassableNavigation(points.current, points.center) && IsPlayerPassableNavigation(points.center, points.next))
                                      {
                                          logging::Info("Nav: Area is player passable!");
                                      }
                                      else
                                      {
                                          logging::Info("Nav: Area is NOT player passable! %.2f,%.2f,%.2f %.2f,%.2f,%.2f %.2f,%.2f,%.2f", points.current.x, points.current.y, points.current.z, points.center.x, points.center.y, points.center.z, points.next.x, points.next.y, points.next.z);
                                      }
                                  });

static CatCommand nav_debug_blacklist("nav_debug_blacklist", "Blacklist connection between two areas for 30s. First area: cat_nav_set Second area: Your location while running this command.",
                                      []()
                                      {
                                          if (!NavEngine::isReady())
                                              return;
                                          auto next    = NavEngine::map->findClosestNavSquare(g_pLocalPlayer->v_Origin);
                                          auto current = NavEngine::map->findClosestNavSquare(loc);

                                          std::pair<CNavArea *, CNavArea *> key(current, next);
                                          NavEngine::map->vischeck_cache[key].expire_tick    = TICKCOUNT_TIMESTAMP(30);
                                          NavEngine::map->vischeck_cache[key].vischeck_state = false;
                                          NavEngine::map->pather.Reset();
                                          logging::Info("Nav: Connection %d->%d Blacklisted.", current->m_id, next->m_id);
                                      });

static InitRoutine init(
    []()
    {
        EC::Register(EC::CreateMove_NoEnginePred, NavEngine::CreateMove, "navengine_cm");
        EC::Register(EC::LevelInit, NavEngine::LevelInit, "navengine_levelinit");
#if ENABLE_VISUALS
        EC::Register(EC::Draw, NavEngine::Draw, "navengine_draw");
#endif
        enabled.installChangeCallback(
            [](settings::VariableBase<bool> &, bool after)
            {
                if (after && g_IEngine->IsInGame())
                    NavEngine::LevelInit();
            });
    });

} // namespace navparser
