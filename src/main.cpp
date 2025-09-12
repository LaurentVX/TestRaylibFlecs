#include "raylib.h"
#include "raymath.h" // Required for Vector3 functions
#include "flecs.h"
#include <vector>
#include <random>
#include <chrono>
#include <iostream>
#include <string>
#include <mutex>
#include <stdarg.h>

//I have removed the #define RAYGUI_IMPLEMENTATION line.This ensures that the implementation is only compiled once in the raygui_impl.cpp file that CMake generates, which will resolve the linker error.
//#define RAYGUI_IMPLEMENTATION
#include "raygui.h"


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
	float entitySize = 10.0f; // Increased entity size
	float entitySpeed = 1500.0f; // Adjusted for better visualization
};

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
struct ColorComp { Color value; };

// --- UI State ---
struct UIState {
    struct nk_context *ctx;
    struct nk_font_atlas *atlas;
};

// --- Helper Functions ---
float GetRandomFloat(float min, float max) {
    static std::random_device rd;
    static std::mt19937 gen(rd());
    std::uniform_real_distribution<float> dis(min, max);
    return dis(gen);
}

Color GetRandomColor() {
    return {
        (unsigned char)GetRandomValue(50, 255),
        (unsigned char)GetRandomValue(50, 255),
        (unsigned char)GetRandomValue(50, 255),
        255
    };
}

void DrawXYGrid(int slices, float spacing) 
{
    int halfSlices = slices/2;
    for (int i = -halfSlices; i <= halfSlices; i++) 
    {
        if (i == 0) 
        {
            DrawLine3D({ -halfSlices*spacing, 0.0f, 0.0f }, { halfSlices*spacing, 0.0f, 0.0f }, BLUE);
            DrawLine3D({ 0.0f, -halfSlices*spacing, 0.0f }, { 0.0f, halfSlices*spacing, 0.0f }, RED);
        } 
        else 
        {
            DrawLine3D({ -halfSlices*spacing, i*spacing, 0.0f }, { halfSlices*spacing, i*spacing, 0.0f }, LIGHTGRAY);
            DrawLine3D({ i*spacing, -halfSlices*spacing, 0.0f }, { i*spacing, halfSlices*spacing, 0.0f }, LIGHTGRAY);
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
        .set<ColorComp>({ GetRandomColor() });

    TraceLog(LOG_INFO, "Created entity %s", new_entity.name().c_str());
}

// --- GUI State ---
struct MyProjectGuiState
{
	int entityCountSpinnerValue = 1;
	bool entityCountSpinnerEditMode = false;
	Rectangle windowBoxRect = { (float)SCREEN_WIDTH - 220, 20, 200, 260 };
};

void DrawGUI(MyProjectGuiState& guiState, flecs::world& world)
{
    // --- Draw GUI ---
    if (GuiWindowBox(guiState.windowBoxRect, "Entity Controls"))
    {
    }

    // Get a mutable reference to the GameState singleton
    GameState& game_state = world.ensure<GameState>();

    GuiLabel({ guiState.windowBoxRect.x + 10, guiState.windowBoxRect.y + 40, 180, 25 }, TextFormat("Total Entities: %d", world.count<Position>()));

    if (GuiSpinner({ guiState.windowBoxRect.x + 10, guiState.windowBoxRect.y + 75, 180, 25 }, "Count:", &guiState.entityCountSpinnerValue, 0, 1000, guiState.entityCountSpinnerEditMode))
    {
        //guiState.entityCountSpinnerEditMode = !guiState.entityCountSpinnerEditMode;
    }

    if (GuiButton({ guiState.windowBoxRect.x + 10, guiState.windowBoxRect.y + 110, 85, 30 }, "Add"))
    {
        for (int i = 0; i < guiState.entityCountSpinnerValue; ++i)
        {
            CreateEntity(world);
        }
    }

    if (GuiButton({ guiState.windowBoxRect.x + 105, guiState.windowBoxRect.y + 110, 85, 30 }, "Remove"))
    {
        auto q = world.query<Position>();
        int count_to_remove = guiState.entityCountSpinnerValue;


		// Query and delete one entity with Health <= 0
        flecs::entity remember;
		world.query<Position>().each([&](flecs::entity e, Position& h)
		{
				//if (h.value <= 0)
				{
					//std::cout << "Deleting: " << e.name() << "\n";
                    TraceLog(LOG_INFO, "Deleting entity %s", e.name().c_str());
					//e.destruct();
                    //e.is_alive(); // false!
                    remember = e;
					return;
				}
		});

        if (remember.is_valid() && remember.is_alive())
		{
			remember.destruct();
        }
    }

    if (GuiButton({ guiState.windowBoxRect.x + 10, guiState.windowBoxRect.y + 150, 180, 30 }, "Remove All"))
    {
        TraceLog(LOG_INFO, "Removing all entities.");
		world.delete_with<Position>(); // Deletes all entities with Position
    }

    GuiSlider({ guiState.windowBoxRect.x + 80, guiState.windowBoxRect.y + 190, 90, 25 }, "Grid Size:", TextFormat("%.0f", game_state.gridSize), &game_state.gridSize, 100.0f, 1000.0f);

    GuiCheckBox({ guiState.windowBoxRect.x + 10, guiState.windowBoxRect.y + 230, 90, 25 }, "Render entities:", &game_state.renderEntities);
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

void DeclareBounceSystem(flecs::world& world)
{
    // System to handle bouncing off grid boundaries, accounting for sphere radius
    world.system<Position, Velocity, ColorComp>("Bounce")
        .each([&](Position& p, Velocity& v, ColorComp& c)
            {
				const GameState& game_state = world.get<GameState>();

                bool bounced = false;
                if ((p.value.x - game_state.entitySize <= -game_state.gridSize && v.value.x < 0) || (p.value.x + game_state.entitySize >= game_state.gridSize && v.value.x > 0))
                {
                    v.value.x *= -1;
                    bounced = true;
                }
                if ((p.value.y - game_state.entitySize <= -game_state.gridSize && v.value.y < 0) || (p.value.y + game_state.entitySize >= game_state.gridSize && v.value.y > 0))
                {
                    v.value.y *= -1;
                    bounced = true;
                }
                // Z-axis check is included for completeness, though movement is 2D
                if ((p.value.z - game_state.entitySize <= -game_state.gridSize && v.value.z < 0) || (p.value.z + game_state.entitySize >= game_state.gridSize && v.value.z > 0))
                {
                    v.value.z *= -1;
                    bounced = true;
                }

                if (bounced)
                {
                    c.value = GetRandomColor();
                }
            });
}

void DeclareMoveSystem(flecs::world& world)
{
    // System to update position based on velocity
    world.system<Position, const Velocity>("Move")
        .each([&](flecs::entity e, Position& p, const Velocity& v)
         {
            const float clampedDeltaTime = std::min(world.delta_time(), 0.33f);
            p.value = Vector3Add(p.value, Vector3Scale(v.value, clampedDeltaTime));
         });
}

void DeclareCollideSystem(flecs::world& world)
{
    // System to handle entity-entity collisions with position correction
    world.system<Position, Velocity, ColorComp>("Collide")
        .each([&](flecs::entity e1, Position& p1, Velocity& v1, ColorComp& c1)
        {
            const GameState& game_state = world.get<GameState>();

            // This inner loop creates pairs of entities for collision checks (e1, e2)
            world.each([&](flecs::entity e2, Position& p2, Velocity& v2, ColorComp& c2)
            {
                if (e1.id() >= e2.id())
                {
                    // Skip self-collision and avoid duplicate pairs
                    return;
                }

                float distance = Vector3Distance(p1.value, p2.value);
                float requiredDistance = game_state.entitySize * 2.0f;

                if (distance < requiredDistance)
                {
                    //TraceLog(LOG_INFO, "Collision between %s and %s", e1.name().c_str(), e2.name().c_str());
                    // 1. Position Correction (Push Apart)
                    float overlap = requiredDistance - distance;
                    Vector3 direction = Vector3Normalize(Vector3Subtract(p1.value, p2.value));
                    if (distance == 0.0f) direction = { 1, 0, 0 }; // Avoid division by zero if perfectly overlapped

                    Vector3 p1_move = Vector3Scale(direction, overlap * 0.5f);
                    Vector3 p2_move = Vector3Scale(direction, -overlap * 0.5f);

                    p1.value = Vector3Add(p1.value, p1_move);
                    p2.value = Vector3Add(p2.value, p2_move);

                    // 2. Velocity Update (Bounce)
                    // Reflect velocity along the collision normal (direction vector)
                    v1.value = Vector3Reflect(v1.value, direction);
                    v2.value = Vector3Reflect(v2.value, Vector3Negate(direction));

                    // 3. Change color on bounce
                    c1.value = GetRandomColor();
                    c2.value = GetRandomColor();
                }
            });
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

void RenderEntities(flecs::world& world)
{
    world.each([&](flecs::entity e, const Position& p, const ColorComp& c)
    {
        const GameState& game_state = world.get<GameState>();
        if (!game_state.renderEntities)
        {
            return;
        }

        DrawSphere(p.value, game_state.entitySize, c.value);
    });
}

bool DoMainGameLoop(bool cameraControlsEnabled, Camera3D camera, flecs::world& world)
{
    // --- Main Game Loop ---
    while (!WindowShouldClose())
    {
        // --- Update ---
        if (IsKeyPressed(KEY_W))
        {
            cameraControlsEnabled = !cameraControlsEnabled;
        }

        if (cameraControlsEnabled)
        {
            UpdateCamera(&camera, CAMERA_FREE /*CAMERA_ORBITAL*/); // Enable orbital camera controls
        }


        const float frameTime = GetFrameTime();
        world.progress(frameTime); // This runs all the systems

        // --- Draw ---
        BeginDrawing();
        ClearBackground(RAYWHITE);

        BeginMode3D(camera);

        const GameState& game_state = world.get<GameState>();

        // Draw the grid on the X/Y plane
        DrawXYGrid(50, 100.0f);
        DrawCubeWiresV({ 0, 0, 0 }, { game_state.gridSize * 2, game_state.gridSize * 2, 0.1f }, DARKGRAY);

        // Render entities
        RenderEntities(world);


        EndMode3D();

        MyProjectGuiState projectGuiState;

        DrawGUI(projectGuiState, world);

        DrawLogPanel();


        DrawText("flecs + raylib | Use mouse to control camera (orbit, zoom, pan)", 10, 10, 20, GREEN);
        DrawFPS(10, 40);

        EndDrawing();
    } return cameraControlsEnabled;
}

int main(void)
{
    // --- Initialization ---
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "flecs + raylib - ECS Collision Demo");
    SetTargetFPS(60);

	// Set custom logger
	SetTraceLogCallback(CustomLog);

    TraceLog(LOG_INFO, "Application started.");

    // --- Flecs World Setup ---
    flecs::world world;


    // singletons
    world.set<GameState>({});

    // --- Systems Definition ---
    DeclareMoveSystem(world);
    DeclareCollideSystem(world);
    DeclareBounceSystem(world);

    CreateInitialEntities(world);




    // --- Raylib Camera Setup ---
    Camera3D camera = { 0 };
    camera.position = { 0.0f, 0.0f, 2000.f }; // Adjusted camera distance
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    bool cameraControlsEnabled = true;


    cameraControlsEnabled = DoMainGameLoop(cameraControlsEnabled, camera, world);


    // --- De-Initialization ---
    CloseWindow();

    return 0;
}