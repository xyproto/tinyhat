// A small 3D world made with raylib. Walk with WASD or the arrow keys and look around with the mouse.
// Press Ctrl+Space in the editor to build, or press it twice quickly to build and run. Esc quits.
#include <raylib.h>
#include <rlgl.h>

#define FIELD 12

static void draw_hat(Vector3 pos)
{
    DrawCylinder((Vector3){ pos.x, pos.y, pos.z }, 1.4f, 1.4f, 0.15f, 24, (Color){ 0xc0, 0x39, 0x2b, 0xff });
    DrawCylinder((Vector3){ pos.x, pos.y + 0.15f, pos.z }, 0.8f, 0.8f, 1.3f, 24, (Color){ 0xc0, 0x39, 0x2b, 0xff });
    DrawCylinder((Vector3){ pos.x, pos.y + 0.35f, pos.z }, 0.82f, 0.82f, 0.25f, 24, (Color){ 0x1a, 0x9e, 0x96, 0xff });
}

static void draw_field(void)
{
    for (int z = -FIELD; z <= FIELD; z++) {
        for (int x = -FIELD; x <= FIELD; x++) {
            Color c = ((x + z) & 1) ? (Color){ 0x20, 0x4a, 0x38, 0xff } : (Color){ 0x2c, 0x5c, 0x45, 0xff };
            DrawPlane((Vector3){ (float)x * 2, 0, (float)z * 2 }, (Vector2){ 2, 2 }, c);
        }
    }
    for (int i = -2; i <= 2; i++) {
        if (i == 0) {
            continue;
        }
        DrawCube((Vector3){ i * 6.0f, 1.5f, -8 }, 1, 3, 1, (Color){ 0x7a, 0xa2, 0xf7, 0xff });
        DrawCubeWires((Vector3){ i * 6.0f, 1.5f, -8 }, 1, 3, 1, (Color){ 0x1a, 0x1b, 0x26, 0xff });
    }
}

int main(void)
{
    Camera3D camera = {
        .position = { 0, 1.7f, 8 },
        .target = { 0, 1, 0 },
        .up = { 0, 1, 0 },
        .fovy = 60,
        .projection = CAMERA_PERSPECTIVE,
    };

    InitWindow(800, 500, "Tiny Hat 3D");
    SetTargetFPS(60);
    DisableCursor();

    while (!WindowShouldClose()) {
        UpdateCamera(&camera, CAMERA_FIRST_PERSON);
        float spin = (float)GetTime() * 40;

        BeginDrawing();
        ClearBackground((Color){ 0x10, 0x14, 0x2c, 0xff });
        BeginMode3D(camera);
        draw_field();
        rlPushMatrix();
        rlRotatef(spin, 0, 1, 0);
        draw_hat((Vector3){ 0, 0, 0 });
        rlPopMatrix();
        EndMode3D();
        DrawText("WASD: walk   Mouse: look   Esc: quit", 10, 10, 20, RAYWHITE);
        DrawFPS(10, 36);
        EndDrawing();
    }

    CloseWindow();
    return 0;
}
