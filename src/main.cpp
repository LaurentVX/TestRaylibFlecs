#include "flecs.h"
#include <vector>
#include <random>
#include <chrono>
#include <iostream>
#include <string>
#include <mutex>
#include <unordered_map>
#include <cstdint>
#include <cmath>

//I have removed the #define RAYGUI_IMPLEMENTATION line.This ensures that the implementation is only compiled once in the raygui_impl.cpp file that CMake generates, which will resolve the linker error.
//#define RAYGUI_IMPLEMENTATION
#include "raygui.h"
#include "raylib.h" 
#include "raymath.h" // Required for Vector3 functions

#define RLIGHTS_IMPLEMENTATION
#include "./rlights.h"
#include "../out/build/x64-Debug/_deps/raylib-build/raylib/include/rlgl.h"
#include "../out/build/x64-Debug/_deps/raylib-src/src/external/glfw/deps/glad/vulkan.h"


#if defined(PLATFORM_DESKTOP)
#define GLSL_VERSION            330
#else   // PLATFORM_ANDROID, PLATFORM_WEB
#define GLSL_VERSION            100
#endif


// --- Log Buffer ---
static std::vector<std::string> logMessages;
static std::mutex logMutex;
static const int MAX_LOG_MESSAGES = 100;


const char* GetLogMsgTypeAsString(int msgType)
{
    switch (msgType)
    {
        case LOG_INFO: return "[INFO]";
        case LOG_ERROR: return "[ERROR]";
        case LOG_WARNING: return "[WARN]";
        case LOG_DEBUG: return "[DEBUG]";
        default: return "";  break;
    }
    return "";
}

// Custom logging function
void CustomLog(int msgType, const char* text, va_list args)
{
    char buffer[1024];
    vsnprintf(buffer, sizeof(buffer), text, args);

    const auto now = std::chrono::system_clock::now();
    std::string formatted_message = std::format("[{:%H:%M:%S}] {} {}", now, GetLogMsgTypeAsString(msgType), buffer);

    std::lock_guard<std::mutex> lock(logMutex);
    if (logMessages.size() >= MAX_LOG_MESSAGES)
    {
        logMessages.erase(logMessages.begin());
    }
    logMessages.push_back(formatted_message);
}


// --- Constants ---
const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;
const float GRID_SIZE = 250.0f; // Using a smaller grid to make collisions more frequent
const int INITIAL_ENTITY_COUNT = 10;



struct GameState
{
    bool renderEntities = true;
    float gridSize = 250.0f;
	float entitySize = 10.0f;
	float entitySpeed = 1500.0f;
};


struct RenderingData
{
    Mesh cube;
    Model model;
    Material material;

    Shader shader;
    std::vector<Matrix> transforms;

    // Per-instance color data
    std::vector<Color> instanceColors;
    Texture2D instanceColorTex = { 0 };

    int locInstanceCount = -1;
    int locInstanceColors = -1;
};


//Camera3D camera, MyProjectGuiState& guiState, flecs::world

// --- GUI State ---
struct MyProjectGuiState
{
	int entityCountSpinnerValue = 1;
	//bool entityCountSpinnerEditMode = false;
	Rectangle windowBoxRect = { (float)SCREEN_WIDTH - 220, 20, 200, 360 };
    int activeTab = 0;
};


struct GameData
{
    RenderingData renderingData;
    Camera3D camera;
    MyProjectGuiState guiState;
    flecs::world* world;

	bool cameraControlsEnabled = true;

	//MyProjectGuiState projectGuiState;
};

// --- Spatial grid cell index for broadphase collision
struct SpatialCell { int cellX; int cellY; };

// --- Buckets for entities per spatial cell (rebuilt every frame)
static std::unordered_map<long long, std::vector<flecs::entity>> g_cellBuckets;

static inline long long CellKey(int x, int y)
{
    return (static_cast<long long>(x) << 32) ^ static_cast<unsigned long long>(y);
}

static inline std::pair<int, int> ComputeCell(const Vector3& p, float cellSize)
{
    const int cx = static_cast<int>(std::floor(p.x / cellSize));
    const int cy = static_cast<int>(std::floor(p.y / cellSize));
    return { cx, cy };
}


flecs::query<const GameState> get_game_state_query(const flecs::world& world)
{
	return world.query_builder<const GameState>()
		.term_at(1)
		//.singleton()
		.cache_kind(flecs::QueryCacheAuto)
		.build();
}

flecs::query<GameState> get_game_state_update_query(const flecs::world& world)
{
	return world.query_builder<GameState>()
        .term_at(1)
        //.singleton()
        .build();
}

// --- Components ---
struct Position { Vector3 value; };
struct Velocity { Vector3 value; };
struct ColorComp
{
    Color value;
};

// Accumulated collision response to apply after broadphase/border checks
struct CollisionResponse
{
    Vector3 posDelta{ 0, 0, 0 };
    Vector3 velDelta{ 0, 0, 0 };
    bool hasCollision{ false };
};

// --- UI State ---
struct UIState
{
    struct nk_context* ctx;
    struct nk_font_atlas* atlas;
};

// --- Helper Functions ---
float GetRandomFloat(float min, float max)
{
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(min, max);
    return dis(gen);
}

Color GetRandomColor()
{
    return {
        (unsigned char)GetRandomValue(50, 255),
        (unsigned char)GetRandomValue(50, 255),
        (unsigned char)GetRandomValue(50, 255),
        255
    };
}

void DrawXYGrid(int slices, float spacing)
{
    int halfSlices = slices / 2;
    for (int i = -halfSlices; i <= halfSlices; i++)
    {
        if (i == 0)
        {
            DrawLine3D({ -halfSlices * spacing, 0.0f, 0.0f }, { halfSlices * spacing, 0.0f, 0.0f }, BLUE);
            DrawLine3D({ 0.0f, -halfSlices * spacing, 0.0f }, { 0.0f, halfSlices * spacing, 0.0f }, RED);
        }
        else
        {
            DrawLine3D({ -halfSlices * spacing, i * spacing, 0.0f }, { halfSlices * spacing, i * spacing, 0.0f }, LIGHTGRAY);
            DrawLine3D({ i * spacing, -halfSlices * spacing, 0.0f }, { i * spacing, halfSlices * spacing, 0.0f }, LIGHTGRAY);
        }
    }
}

// --- Entity Management Function ---
void CreateEntity(flecs::world& world)
{
    Vector3 newPos;
    bool positionIsValid;
    int maxRetries = 100;
    int retries = 0;

    const GameState& game_state = world.get<GameState>();


    do
    {
        positionIsValid = true;
        newPos =
        {
            GetRandomFloat(-game_state.gridSize + game_state.entitySize, game_state.gridSize - game_state.entitySize),
            GetRandomFloat(-game_state.gridSize + game_state.entitySize, game_state.gridSize - game_state.entitySize),
            0.0f
        };
        world.each([&](flecs::entity existing_entity, const Position& existing_pos)
            {
                if (Vector3Distance(newPos, existing_pos.value) < game_state.entitySize * 2.0f)
                {
                    positionIsValid = false;
                }
            });
        retries++;
    } while (!positionIsValid && retries < maxRetries);

    if (!positionIsValid)
    {
        TraceLog(LOG_WARNING, "Failed to find a valid position for new entity after %d retries.", maxRetries);
        return; // Failed to find a spot
    }

    Vector3 randomVelocity = { GetRandomFloat(-1.0f, 1.0f), GetRandomFloat(-1.0f, 1.0f), 0.0f };

    auto new_entity = world.entity()
        .set<Position>({ newPos })
        .set<Velocity>({ Vector3Scale(Vector3Normalize(randomVelocity), game_state.entitySpeed) })
        .set<ColorComp>({ GetRandomColor() })
        .set<SpatialCell>({ 0, 0 })
        .set<CollisionResponse>({ Vector3Zero(), Vector3Zero(), false });

    TraceLog(LOG_INFO, "Created entity %s", new_entity.name().c_str());
}

#define MYARRAYSIZE(x) (sizeof((x)) / sizeof((x)[0]))

void DrawGUI(MyProjectGuiState& guiState, flecs::world& world)
{
    // --- Draw GUI ---
    if (GuiWindowBox(guiState.windowBoxRect, "Entity Controls"))
    {
    }

    float yOffset = guiState.windowBoxRect.y;

    //static const char* TabNames[] = { "Tab1","Tab2", "Tab3" };
    yOffset += 30.f;
    int TabBarResult = GuiToggleGroup({ guiState.windowBoxRect.x + 10, yOffset, 40, 25 }, "Tab1;Tab2;Tab3;Tab4", &guiState.activeTab);
    if (guiState.activeTab == 0)
    {
        // Get a mutable reference to the GameState singleton
        GameState& game_state = world.ensure<GameState>();

        yOffset += 60.f;
        GuiLabel({ guiState.windowBoxRect.x + 10, yOffset, 120, 25 }, TextFormat("Total Entities: %d", world.count<Position>()));

        yOffset += 30.f;
	    int newCount = GuiSpinner({ guiState.windowBoxRect.x + 10, yOffset, 120, 25 }, "Add/Remove", &guiState.entityCountSpinnerValue, 1, 100, false);
    // 	if (newCount)
    //     {
    //         guiState.entityCountSpinnerValue += newCount;
    //     }
    
        yOffset += 30.f;
        if (GuiButton({ guiState.windowBoxRect.x + 10, yOffset, 85, 30 }, "Add"))
        {
            for (int i = 0; i < guiState.entityCountSpinnerValue; ++i)
            {
                CreateEntity(world);
            }
        }

        yOffset += 30.f;
        if (GuiButton({ guiState.windowBoxRect.x + 105, yOffset, 85, 30 }, "Remove"))
        {
    //         auto q = world.query<Position>();
    //         int count_to_remove = guiState.entityCountSpinnerValue;


		    // Query and delete one entity
    //       flecs::entity toDelete;
    //       world.query<Position>().run([&](flecs::iter& it)
    //       {
    //           while (it.next())
    //           {
    //               for (auto i : it)
    //               {
    //                   flecs::entity e = it.entity(i);
    //                   toDelete = e;
    //                   return;
    //               }
    //           }
    //       });

		    flecs::entity toDelete;
            world.query<Position>().each([&](flecs::entity e, Position& h)
            {
			    toDelete = e;
                return;
		    });

            if (toDelete.is_valid() && toDelete.is_alive())
		    {
                TraceLog(LOG_INFO, "Deleting entity %s", toDelete.name().c_str());
			    toDelete.destruct();
            }
        }

        yOffset += 30.f;
        if (GuiButton({ guiState.windowBoxRect.x + 10, yOffset, 180, 30 }, "Remove All"))
        {
            TraceLog(LOG_INFO, "Removing all entities.");
		    world.delete_with<Position>(); // Deletes all entities with Position
        }

        yOffset += 30.f;
        GuiSlider({ guiState.windowBoxRect.x + 80, yOffset, 90, 25 }, "Grid Size:", TextFormat("%.0f", game_state.gridSize), &game_state.gridSize, 100.0f, 1000.0f);

        yOffset += 30.f;
	    GuiSlider({ guiState.windowBoxRect.x + 80, yOffset, 90, 25 }, "Entity Size:", TextFormat("%.0f", game_state.entitySize), &game_state.entitySize, 0.01f, 100.0f);

        yOffset += 30.f;
        if (GuiSlider({ guiState.windowBoxRect.x + 80, yOffset, 90, 25 }, "Entity speed:", TextFormat("%.0f", game_state.entitySpeed), &game_state.entitySpeed, 0.f, 5000.f))
        {
            world.modified<GameState>();
        }

        yOffset += 30.f;
        GuiCheckBox({ guiState.windowBoxRect.x + 10, yOffset, 40, 25 }, "Render entities:", &game_state.renderEntities);
	}
	else if (guiState.activeTab == 1)
	{
		// Get a mutable reference to the GameState singleton
		GameState& game_state = world.ensure<GameState>();

		yOffset += 30.f;
		GuiSlider({ guiState.windowBoxRect.x + 80, yOffset, 90, 25 }, "Flecs logs level:", TextFormat("%.0f", game_state.entitySize), &game_state.entitySize, 0.01f, 100.0f);

	}
}

void DrawLogPanel()
{
    static Vector2 scrollPos = { 0, 0 };
    static Rectangle panelRec = { 0, SCREEN_HEIGHT - 120, (float)SCREEN_WIDTH, 120 };
    Rectangle panelContentRec;
    {
        std::lock_guard<std::mutex> lock(logMutex);
        panelContentRec = { 0, 0, panelRec.width - 20, (float)logMessages.size() * 20 };
    }
    static Rectangle panelView = { 0 };

    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT, TEXT_ALIGN_LEFT);
    GuiScrollPanel(panelRec, "Logs", panelContentRec, &scrollPos, &panelView);

    // Draw clear button
    Rectangle clearButtonRec = { panelRec.x + panelRec.width - 80, panelRec.y + 2, 70, 20 };
    if (GuiButton(clearButtonRec, "Clear"))
    {
        std::lock_guard<std::mutex> lock(logMutex);
        logMessages.clear();
        // Optionally, add a log message to confirm clearing
        // This would require re-locking or a more complex log call
    }

    BeginScissorMode((int)panelView.x, (int)panelView.y, (int)panelView.width, (int)panelView.height);
    {
        std::lock_guard<std::mutex> lock(logMutex);
        for (size_t i = 0; i < logMessages.size(); ++i)
        {
            Rectangle itemRec = 
            {
                panelRec.x + 10,
                panelRec.y + 10 + i * 20 - scrollPos.y,
                panelRec.width - 30,
                20
            };
            // Make sure to only draw items within the view
            if (CheckCollisionRecs(panelView, itemRec))
            {
                GuiLabel(itemRec, logMessages[i].c_str());
            }
        }
    }
    EndScissorMode();
    GuiSetStyle(DEFAULT, TEXT_ALIGNMENT, TEXT_ALIGN_CENTER);
}





void DeclareGameStateObserver(flecs::world& world)
{
    world.observer<GameState>()
        //.kind<StateObserverSystem>()
        .event(flecs::OnSet)
        .each([&](flecs::entity e, GameState& gs)
        {
            // update velocity

            //std::cout << "Position set: {" << p.x << ", " << p.y << "}\n";
				world.query<Velocity>().each([&](flecs::entity e, Velocity& v)
				{
                    v = { Vector3Scale(Vector3Normalize(v.value), gs.entitySpeed) };
				});
        });
}

struct BounceSystem
{
};
void DeclareDetectGridEntityCollision(flecs::world& world, const flecs::entity& inPhase)
{
    world.system<Position, Velocity, ColorComp, CollisionResponse>("DetectGridEntity")
        .kind(inPhase)
        .read<Position>()
        .read<Velocity>()
        .write<CollisionResponse>()
        .each([&](Position& p, Velocity& v, ColorComp& /*c*/, CollisionResponse& resp)
        {
            const GameState& game_state = world.get<GameState>();
            bool bounced = false;
            Vector3 normal{ 0, 0, 0 };
            Vector3 posFix{ 0, 0, 0 };

            // Left
            if (p.value.x - game_state.entitySize < -game_state.gridSize && v.value.x < 0)
            {
                float overlap = (-game_state.gridSize + game_state.entitySize) - p.value.x;
                posFix.x += overlap;
                normal = Vector3Add(normal, Vector3{ 1, 0, 0 });
                bounced = true;
            }
            // Right
            if (p.value.x + game_state.entitySize > game_state.gridSize && v.value.x > 0)
            {
                float overlap = (game_state.gridSize - game_state.entitySize) - p.value.x;
                posFix.x += overlap;
                normal = Vector3Add(normal, Vector3{ -1, 0, 0 });
                bounced = true;
            }
            // Bottom
            if (p.value.y - game_state.entitySize < -game_state.gridSize && v.value.y < 0)
            {
                float overlap = (-game_state.gridSize + game_state.entitySize) - p.value.y;
                posFix.y += overlap;
                normal = Vector3Add(normal, Vector3{ 0, 1, 0 });
                bounced = true;
            }
            // Top
            if (p.value.y + game_state.entitySize > game_state.gridSize && v.value.y > 0)
            {
                float overlap = (game_state.gridSize - game_state.entitySize) - p.value.y;
                posFix.y += overlap;
                normal = Vector3Add(normal, Vector3{ 0, -1, 0 });
                bounced = true;
            }

            if (bounced)
            {
                // Normalize the combined normal if any component present
                if (Vector3Length(normal) > 0.0f) normal = Vector3Normalize(normal);
                // Compute reflected velocity; store as delta
                Vector3 reflected = Vector3Reflect(v.value, normal);
                resp.posDelta = Vector3Add(resp.posDelta, posFix);
                resp.velDelta = Vector3Add(resp.velDelta, Vector3Subtract(reflected, v.value));
                resp.hasCollision = true;
            }
        });
}

void DeclareMoveEntitiesSystem(flecs::world& world, const flecs::entity& inPhase)
{
    // System to update position based on velocity
    world.system<Position, const Velocity>("MoveEntities")
		.multi_threaded()
        .kind(inPhase)
		.read<Velocity>()
		.write<Position>()
        .each([&](flecs::entity e, Position& p, const Velocity& v)
         {
            const float clampedDeltaTime = std::min(world.delta_time(), 0.33f);
            p.value = Vector3Add(p.value, Vector3Scale(v.value, clampedDeltaTime));
         });
}

// Clear spatial buckets once per frame before filling them
void DeclareClearSpatialBucketsSystem(flecs::world& world, const flecs::entity& inPhase)
{
    world.system<>("ClearSpatialBuckets")
        .kind(inPhase)
        .each([&]()
        {
            g_cellBuckets.clear();
        });
}

// Update spatial cell for each entity and fill buckets
void DeclareUpdateSpatialCellSystem(flecs::world& world, const flecs::entity& inPhase)
{
    world.system<const Position, SpatialCell>("UpdateSpatialCell")
        //.multi_threaded()
        .kind(inPhase)
        .read<Position>()
        .write<SpatialCell>()
        .each([&](flecs::entity e, const Position& p, SpatialCell& sc)
        {
            const GameState& game_state = world.get<GameState>();
            const float cellSize = std::max(game_state.entitySize * 2.0f, 1.0f);
            auto [cx, cy] = ComputeCell(p.value, cellSize);
            sc.cellX = cx;
            sc.cellY = cy;
            const int32_t cellIndex = CellKey(cx, cy);
           // assert(cellIndex)
            g_cellBuckets[cellIndex].push_back(e); // can cause thread collision
        });
}

 void DeclareDetectEntitiesCollision(flecs::world& world, const flecs::entity& inPhase)
{
    // Broadphase collision: record responses instead of directly mutating P/V
    world.system<const Position, const Velocity, const SpatialCell, CollisionResponse>("DetectEntitiesCollision")
        .kind(inPhase) // keep single-threaded to avoid cross-entity write races
        .read<Position>()
        .read<Velocity>()
        .read<SpatialCell>()
        .write<CollisionResponse>()
        .each([&](flecs::entity e1, const Position& p1, const Velocity& v1, const SpatialCell& sc1, CollisionResponse& r1)
        {
            const GameState& game_state = world.get<GameState>();
            const float requiredDistance = game_state.entitySize * 2.0f;

            for (int dy = -1; dy <= 1; ++dy)
            {
                for (int dx = -1; dx <= 1; ++dx)
                {
                    const int nx = sc1.cellX + dx;
                    const int ny = sc1.cellY + dy;
                    auto it = g_cellBuckets.find(CellKey(nx, ny));
                    if (it == g_cellBuckets.end()) continue;

                    const auto& bucket = it->second;
                    for (const flecs::entity& e2 : bucket)
                    {
                        if (e1.id() >= e2.id()) continue; // process pair once

                        auto p2 = e2.get<Position>();
                        auto v2 = e2.get<Velocity>();
                        //if (!p2 || !v2) continue;

                        const float distance = Vector3Distance(p1.value, p2.value);
                        if (distance < requiredDistance)
                        {
                            const float overlap = requiredDistance - distance;
                            const Vector3 direction = (distance > 0.0f)
                                ? Vector3Normalize(Vector3Subtract(p1.value, p2.value))
                                : Vector3{ 1, 0, 0 };

                            const Vector3 p1_move = Vector3Scale(direction, overlap * 0.5f);
                            const Vector3 p2_move = Vector3Scale(direction, -overlap * 0.5f);

                            // Record response for e1
                            const Vector3 v1_reflect = Vector3Reflect(v1.value, direction);
                            r1.posDelta = Vector3Add(r1.posDelta, p1_move);
                            r1.velDelta = Vector3Add(r1.velDelta, Vector3Subtract(v1_reflect, v1.value));
                            r1.hasCollision = true;

                            // Record response for e2
                            auto r2 = e2.get_mut<CollisionResponse>();
                            r2.posDelta = Vector3Add(r2.posDelta, p2_move);
                            const Vector3 v2_reflect = Vector3Reflect(v2.value, Vector3Negate(direction));
                            r2.velDelta = Vector3Add(r2.velDelta, Vector3Subtract(v2_reflect, v2.value));
                            r2.hasCollision = true;
                        }
                    }
                }
            }
        });
 }

// Apply accumulated responses and reset
void DeclareApplyCollisionResponseSystem(flecs::world& world, const flecs::entity& inPhase)
{
    world.system<Position, Velocity, ColorComp, CollisionResponse>("ApplyCollisionResponse")
        .kind(inPhase)
        .write<Position>()
        .write<Velocity>()
        .write<ColorComp>()
        .write<CollisionResponse>()
        .each([&](Position& p, Velocity& v, ColorComp &c, CollisionResponse& resp)
        {
            if (!resp.hasCollision)
                return;

            p.value = Vector3Add(p.value, resp.posDelta);
            v.value = Vector3Add(v.value, resp.velDelta);
            c.value = GetRandomColor();

            // reset accumulator
            resp.posDelta = Vector3Zero();
            resp.velDelta = Vector3Zero();
            resp.hasCollision = false;
        });
 }


 void CreateInitialEntities(flecs::world ecs)
 {
     // --- Entity Creation with overlap prevention ---
     for (int i = 0; i < INITIAL_ENTITY_COUNT; ++i)
     {
         CreateEntity(ecs);
     }
 }

 void RenderEntities(GameData& gameData)
 {
	const GameState& game_state = gameData.world->get<GameState>();
	if (!game_state.renderEntities)
	{
		return;
	}

    auto q = gameData.world->query<Position>();
	int count = q.count();

    gameData.renderingData.transforms.resize(count);

    int index = 0;
    gameData.world->each([&](flecs::entity e, const Position& p, const ColorComp& c)
    {

        Matrix& EntityTransform = gameData.renderingData.transforms[index];
        //EntityTransform = ;
        EntityTransform = MatrixScale(game_state.entitySize, game_state.entitySize, game_state.entitySize) * MatrixTranslate(p.value.x, p.value.y, p.value.z);
        index++;

        //DrawSphere(p.value, game_state.entitySize, c.value);
    });


	// Draw meshes instanced using material containing instancing shader (RED + lighting),
    // transforms[] for the instances should be provided, they are dynamically
    // updated in GPU every frame, so we can animate the different mesh instances
// 	DrawMeshInstanced(gameData.renderingData.cube,
//         gameData.renderingData.matInstances, gameData.renderingData.transforms.data(), MAX_INSTANCES);

//     static Matrix* transforms = (Matrix*)RL_CALLOC(1, sizeof(Matrix));
//     transforms[0] = MatrixIdentity();;
// 
//     Matrix matrixIdentity = MatrixIdentity();
//     Material matDefault = LoadMaterialDefault();
//     matDefault.maps[MATERIAL_MAP_DIFFUSE].color = BLUE;
//     DrawMeshInstanced(gameData.renderingData.cube, matDefault, transforms, 1);

 	DrawMeshInstanced(gameData.renderingData.cube, 
         gameData.renderingData.material,
         gameData.renderingData.transforms.data(),
         gameData.renderingData.transforms.size());
 }

 void DoMainGameLoop(GameData& gameData)
 {
     // --- Main Game Loop ---
     while (!WindowShouldClose())
     {
         // --- Update ---
         if (IsKeyPressed(KEY_W))
         {
             gameData.cameraControlsEnabled = !gameData.cameraControlsEnabled;
         }

         if (gameData.cameraControlsEnabled)
         {
             UpdateCamera(&gameData.camera, CAMERA_FREE /*CAMERA_ORBITAL*/); // Enable orbital camera controls
         }


         const float frameTime = GetFrameTime();
         gameData.world->progress(frameTime); // This runs all the systems

         // --- Draw ---
         BeginDrawing();
         ClearBackground(RAYWHITE);

         BeginMode3D(gameData.camera);

         const GameState& game_state = gameData.world->get<GameState>();

         // Draw the grid on the X/Y plane
         DrawXYGrid(50, 100.0f);
         DrawCubeWiresV({ 0, 0, 0 }, { game_state.gridSize * 2, game_state.gridSize * 2, 0.1f }, DARKGRAY);

         // Render entities
         RenderEntities(gameData);


         EndMode3D();



         DrawGUI(gameData.guiState, *gameData.world);

         DrawLogPanel();


         DrawText("flecs + raylib | Use mouse to control camera (orbit, zoom, pan)", 10, 10, 20, GREEN);
         DrawFPS(10, 40);

         EndDrawing();
     }
 }

 void InitCamera3D(Camera3D& camera)
 {
     camera = { 0 };
     camera.position = { 0.0f, 0.0f, 2000.f }; // Adjusted camera distance
     camera.target = { 0.0f, 0.0f, 0.0f };
     camera.up = { 0.0f, 1.0f, 0.0f };
     camera.fovy = 45.0f;
     camera.projection = CAMERA_PERSPECTIVE;
 }


// /** OS API log function type. */
// typedef
// void (*ecs_os_api_log_t)(
// 	int32_t level,     /* Logging level */
// 	const char* file,  /* File where message was logged */
// 	int32_t line,      /* Line it was logged */
// 	const char* msg);

 void OnFlecsLogCallback(int level, const char* file, int32_t line, const char* msg)
 {
	// Example: print only warnings and errors
	if (level >= 1)
	{
		fprintf(stderr, "Flecs Log [Level %d]: %s\n", level, msg);

		TraceLog(level, msg);
	}


 }


 void InitFlecs(GameData& gameData)
 {
	gameData.world = new flecs::world();

	ecs_os_api.log_ = OnFlecsLogCallback;

	flecs::log::set_level(3);


    gameData.world->set_threads(4);
 }


 void InitRenderingData(RenderingData& renderingData)
 {

    std::string lighting_instancing_vs = TextFormat("res/shaders/glsl%i/lighting_instancing.vs", GLSL_VERSION);
    std::string lighting_fs = TextFormat("res/shaders/glsl%i/lighting.fs", GLSL_VERSION);

    if (!FileExists(lighting_instancing_vs.data()))
    {
        TraceLog(LOG_ERROR, "lighting_instancing_vs is invalid");
    }

	if (!FileExists(lighting_fs.data()))
	{
		TraceLog(LOG_ERROR, "lighting_fs is invalid");
	}

	// Load lighting shader
	Shader shader = LoadShader(lighting_instancing_vs.data(), lighting_fs.data());
	// Get shader locations
	shader.locs[SHADER_LOC_MATRIX_MVP] = GetShaderLocation(shader, "mvp");
	shader.locs[SHADER_LOC_VECTOR_VIEW] = GetShaderLocation(shader, "viewPos");

	// Set shader value: ambient light level
	int ambientLoc = GetShaderLocation(shader, "ambient");
    static const float ambientValues[] = { 0.2f, 0.2f, 0.2f, 1.0f };
	SetShaderValue(shader, ambientLoc, ambientValues, SHADER_UNIFORM_VEC4);

 	// Create one light
    const Vector3 lightValue = { 50.0f, 50.0f, 0.0f };
 	CreateLight(LIGHT_DIRECTIONAL, lightValue, Vector3Zero(), WHITE, shader);

	// NOTE: We are assigning the intancing shader to material.shader
	// to be used on mesh drawing with DrawMeshInstanced()
	Material matInstances = LoadMaterialDefault();
	matInstances.shader = shader;
	matInstances.maps[MATERIAL_MAP_DIFFUSE].color = RED;

    renderingData.cube = GenMeshSphere(1.f, 32, 32);
    renderingData.material = matInstances;
    renderingData.shader = shader;

    // Get uniforms for per-instance coloring
    renderingData.locInstanceCount = GetShaderLocation(renderingData.shader, "uInstanceCount");
    renderingData.locInstanceColors = GetShaderLocation(renderingData.shader, "uInstanceColors");
 }


 void DeclareECS(GameData& gameData)
 {
     flecs::world* world = gameData.world;

    // declare the phases
    flecs::entity PrePhysics = world->entity("PrePhysics").add(flecs::Phase);
    flecs::entity Physics = world->entity("Physics").add(flecs::Phase).depends_on(PrePhysics);
    flecs::entity PostPhysics = world->entity("PostPhysics").add(flecs::Phase).depends_on(Physics);

    DeclareGameStateObserver(*gameData.world);

    // Pre-physics: build spatial grid and resolve collision responses
    DeclareClearSpatialBucketsSystem(*gameData.world, PrePhysics);
    DeclareUpdateSpatialCellSystem(*gameData.world, PrePhysics);

    DeclareDetectEntitiesCollision(*gameData.world, PrePhysics);
    DeclareDetectGridEntityCollision(*gameData.world, PrePhysics);

    DeclareApplyCollisionResponseSystem(*gameData.world, PrePhysics);

    // Integrate after applying collision responses
    DeclareMoveEntitiesSystem(*gameData.world, PrePhysics);

    // singletons
    gameData.world->set<GameState>({});
 }


 int main(void)
 {

    GameData gameData;

    // --- Initialization ---
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "flecs + raylib - ECS Collision Demo");
    SetTargetFPS(60);

	// Set custom logger
	SetTraceLogCallback(CustomLog);

    TraceLog(LOG_INFO, "Application started.");

    // --- Flecs World Setup ---
    InitFlecs(gameData);

    InitRenderingData(gameData.renderingData);


    // --- Systems Definition ---
    DeclareECS(gameData);


    CreateInitialEntities(*gameData.world);

    // --- Raylib Camera Setup ---
    InitCamera3D(gameData.camera);




    DoMainGameLoop(gameData);


    // --- De-Initialization ---
    CloseWindow();

    return 0;
 }