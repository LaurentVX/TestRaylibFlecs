#include "raylib.h"
#include "raymath.h" // Required for Vector3 functions
#include "flecs.h"
#include <vector>
#include <random>

// --- Constants ---
const int SCREEN_WIDTH = 1280;
const int SCREEN_HEIGHT = 720;
const float GRID_SIZE = 250.0f; // Using a smaller grid to make collisions more frequent
const int ENTITY_COUNT = 10;
const float ENTITY_SIZE = 10.0f; // Increased entity size
const float ENTITY_SPEED = 1500.0f; // Adjusted for better visualization

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

void DrawXYGrid(int slices, float spacing) {
    int halfSlices = slices/2;
    for (int i = -halfSlices; i <= halfSlices; i++) {
        if (i == 0) {
            DrawLine3D({ -halfSlices*spacing, 0.0f, 0.0f }, { halfSlices*spacing, 0.0f, 0.0f }, BLUE);
            DrawLine3D({ 0.0f, -halfSlices*spacing, 0.0f }, { 0.0f, halfSlices*spacing, 0.0f }, RED);
        } else {
            DrawLine3D({ -halfSlices*spacing, i*spacing, 0.0f }, { halfSlices*spacing, i*spacing, 0.0f }, LIGHTGRAY);
            DrawLine3D({ i*spacing, -halfSlices*spacing, 0.0f }, { i*spacing, halfSlices*spacing, 0.0f }, LIGHTGRAY);
        }
    }
}

// --- Entity Management Function ---
void CreateEntity(flecs::world& ecs) {
    Vector3 newPos;
    bool positionIsValid;
    int maxRetries = 100;
    int retries = 0;

    do {
        positionIsValid = true;
        newPos = { 
            GetRandomFloat(-GRID_SIZE + ENTITY_SIZE, GRID_SIZE - ENTITY_SIZE), 
            GetRandomFloat(-GRID_SIZE + ENTITY_SIZE, GRID_SIZE - ENTITY_SIZE), 
            0.0f 
        };
        ecs.each([&](flecs::entity existing_entity, const Position& existing_pos) {
            if (Vector3Distance(newPos, existing_pos.value) < ENTITY_SIZE * 2.0f) {
                positionIsValid = false;
            }
        });
        retries++;
    } while (!positionIsValid && retries < maxRetries);

    if (!positionIsValid) return; // Failed to find a spot

    Vector3 randomVelocity = { GetRandomFloat(-1.0f, 1.0f), GetRandomFloat(-1.0f, 1.0f), 0.0f };
    
    ecs.entity()
        .set<Position>({ newPos })
        .set<Velocity>({ Vector3Scale(Vector3Normalize(randomVelocity), ENTITY_SPEED) })
        .set<ColorComp>({ GetRandomColor() });
}

int main(void) {
    // --- Initialization ---
    SetConfigFlags(FLAG_MSAA_4X_HINT);
    InitWindow(SCREEN_WIDTH, SCREEN_HEIGHT, "flecs + raylib - ECS Collision Demo");
    SetTargetFPS(60);

    // --- Flecs World Setup ---
    flecs::world ecs;

    // --- Systems Definition ---

    // System to update position based on velocity
    ecs.system<Position, const Velocity>("Move")
        .each([&](flecs::entity e, Position& p, const Velocity& v) 
    {
        p.value = Vector3Add(p.value, Vector3Scale(v.value, ecs.delta_time()));
    });

    // System to handle entity-entity collisions with position correction
    ecs.system<Position, Velocity, ColorComp>("Collide")
        .each([&](flecs::entity e1, Position& p1, Velocity& v1, ColorComp& c1) 
    {
        // This inner loop creates pairs of entities for collision checks (e1, e2)
        ecs.each([&](flecs::entity e2, Position& p2, Velocity& v2, ColorComp& c2) 
        {
            if (e1.id() >= e2.id()) 
            {
                // Skip self-collision and avoid duplicate pairs
                return;
            }

            float distance = Vector3Distance(p1.value, p2.value);
            float requiredDistance = ENTITY_SIZE * 2.0f;

            if (distance < requiredDistance) 
            {
                // 1. Position Correction (Push Apart)
                float overlap = requiredDistance - distance;
                Vector3 direction = Vector3Normalize(Vector3Subtract(p1.value, p2.value));
                if (distance == 0.0f) direction = {1, 0, 0}; // Avoid division by zero if perfectly overlapped

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

    // System to handle bouncing off grid boundaries, accounting for sphere radius
    ecs.system<Position, Velocity, ColorComp>("Bounce")
        .each([](Position& p, Velocity& v, ColorComp& c) 
    {
        bool bounced = false;
        if ((p.value.x - ENTITY_SIZE <= -GRID_SIZE && v.value.x < 0) || (p.value.x + ENTITY_SIZE >= GRID_SIZE && v.value.x > 0)) 
        {
            v.value.x *= -1;
            bounced = true;
        }
        if ((p.value.y - ENTITY_SIZE <= -GRID_SIZE && v.value.y < 0) || (p.value.y + ENTITY_SIZE >= GRID_SIZE && v.value.y > 0)) 
        {
            v.value.y *= -1;
            bounced = true;
        }
        // Z-axis check is included for completeness, though movement is 2D
        if ((p.value.z - ENTITY_SIZE <= -GRID_SIZE && v.value.z < 0) || (p.value.z + ENTITY_SIZE >= GRID_SIZE && v.value.z > 0)) 
        {
            v.value.z *= -1;
            bounced = true;
        }

        if (bounced) {
            c.value = GetRandomColor();
        }
    });

    // --- Entity Creation with overlap prevention ---
    for (int i = 0; i < ENTITY_COUNT; ++i) 
    {
        Vector3 newPos;
        bool positionIsValid;
        int maxRetries = 100; // Prevent infinite loop in dense scenarios
        int retries = 0;

        do 
        {
            positionIsValid = true;
            newPos = 
            { 
                GetRandomFloat(-GRID_SIZE + ENTITY_SIZE, GRID_SIZE - ENTITY_SIZE), 
                GetRandomFloat(-GRID_SIZE + ENTITY_SIZE, GRID_SIZE - ENTITY_SIZE), 
                0.0f 
            };

            // Check against all previously created entities
            ecs.each([&](flecs::entity existing_entity, const Position& existing_pos) 
            {
                if (Vector3Distance(newPos, existing_pos.value) < ENTITY_SIZE * 2.0f) 
                {
                    positionIsValid = false;
                }
            });
            retries++;
        } while (!positionIsValid && retries < maxRetries);

        // Create a random velocity vector and normalize it to ensure constant speed
        Vector3 randomVelocity = 
        {
            GetRandomFloat(-1.0f, 1.0f),
            GetRandomFloat(-1.0f, 1.0f),
            0.0f // No movement on the Z-axis
        };
        
        ecs.entity()
            .set<Position>({ newPos })
            .set<Velocity>({Vector3Scale(Vector3Normalize(randomVelocity), ENTITY_SPEED) })
            .set<ColorComp>({ GetRandomColor() });
    }

    // --- Raylib Camera Setup ---
    Camera3D camera = { 0 };
    camera.position = { 0.0f, 0.0f, 2000.f }; // Adjusted camera distance
    camera.target = { 0.0f, 0.0f, 0.0f };
    camera.up = { 0.0f, 1.0f, 0.0f };
    camera.fovy = 45.0f;
    camera.projection = CAMERA_PERSPECTIVE;

    // --- Main Game Loop ---
    while (!WindowShouldClose()) 
    {
        // --- Update ---
        UpdateCamera(&camera, CAMERA_FREE /*CAMERA_ORBITAL*/); // Enable orbital camera controls

        const float frameTime = GetFrameTime();
        ecs.progress(frameTime); // This runs all the systems

        // --- Draw ---
        BeginDrawing();
        ClearBackground(RAYWHITE);

        BeginMode3D(camera);

        // Draw the grid on the X/Y plane
        DrawXYGrid(50, 100.0f);
        DrawCubeWiresV({0, 0, 0}, {GRID_SIZE * 2, GRID_SIZE * 2, 0.1f}, DARKGRAY);

        // Render entities
        ecs.each([&](flecs::entity e, const Position& p, const ColorComp& c) 
        {
            DrawSphere(p.value, ENTITY_SIZE, c.value);
        });

        EndMode3D();

        DrawText("flecs + raylib | Use mouse to control camera (orbit, zoom, pan)", 10, 10, 20, GREEN);
        DrawFPS(10, 40);

        EndDrawing();
    }

    // --- De-Initialization ---
    CloseWindow();

    return 0;
}