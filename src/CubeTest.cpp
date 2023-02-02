/* 3-D animation program to rotate 12 cubes. Uses fixed point. All C code
   tested with Borland C++ in C compilation mode and the small model. */
#ifndef UNICODE
#define UNICODE
#endif

#include <windows.h>

#include <new>
#include <chrono>
#include <stdlib.h>
#include <iostream>
#include <conio.h>   // _getch()
#include <io.h>      // _open_osfhandle()
#include <fcntl.h>   // _O_TEXT
#include <string>    // to_string

#ifndef M_PI
#define _USE_MATH_DEFINES  // for M_PI
#include <math.h>          // sin(), cos(), sqrt()
#endif


#include "RenderFXP.h"
#include "random.h"
#include "Canvas32.h"
#include "GdiWindow.h"
#include "SpadSim.h"
#include "cow.h"

#define NUM_CUBE_VERTS  8 /* # of vertices per cube */
#define NUM_CUBE_FACES  6 /* # of faces per cube */
#define NUM_CUBES      12 /* # of cubes */

int NumObjects = 0;
int RecalcAllXforms = 1;

//Xform WorldViewXform;           // initialized from floats
PObject* ObjectList[NUM_CUBES];   // pointers to objects
Point3 CubeVerts[NUM_CUBE_VERTS]; // set elsewhere, from floats

#ifndef ARRAYSIZE
#define ARRAYSIZE(x) (sizeof(x) / sizeof(x[0]))
#endif

////////////////////////////////////////////////////////////////////////////////
// Transform from world space into view space (no transformation, currently)
void InitializeFixedPoint()
{
    /*
    int IntWorldViewXform[3][4] = {{1,0,0,0},
                                   {0,1,0,0},
                                   {0,0,1,0}};
    for (int i=0; i<3; i++)
        for (int j=0; j<4; j++)
            WorldViewXform[i][j] = INT_TO_FIXED(IntWorldViewXform[i][j]);
            */

    // All vertices in the basic cube
    static IntPoint3 IntCubeVerts[NUM_CUBE_VERTS] = {
        {15,15,15}, {15,15,-15}, {15,-15,15}, {15,-15,-15},
       {-15,15,15},{-15,15,-15},{-15,-15,15},{-15,-15,-15} };

    for (int i=0; i < NUM_CUBE_VERTS; i++) {
        CubeVerts[i].X = INT_TO_FIXED(IntCubeVerts[i].X);
        CubeVerts[i].Y = INT_TO_FIXED(IntCubeVerts[i].Y);
        CubeVerts[i].Z = INT_TO_FIXED(IntCubeVerts[i].Z);
    }
}

////////////////////////////////////////////////////////////////////////////////
// Initializes the cubes and adds them to the object list.

// vertex indices for individual cube faces
static int Face1[] = {1,3,2,0};
static int Face2[] = {5,7,3,1};
static int Face3[] = {4,5,1,0};
static int Face4[] = {3,7,6,2};
static int Face5[] = {5,4,6,7};
static int Face6[] = {0,2,6,4};

static int *VertNumList[] = { Face1, Face2, Face3, Face4, Face5, Face6 };

static int VertsInFace[]={
    ARRAYSIZE(Face1),
    ARRAYSIZE(Face2),
    ARRAYSIZE(Face3),
    ARRAYSIZE(Face4),
    ARRAYSIZE(Face5),
    ARRAYSIZE(Face6) };

/* X, Y, Z rotations for cubes */
#define ROT_6  INT_TO_FIXED(3)  /* rotate 6 degrees at a time */
#define ROT_3  INT_TO_FIXED(2)  /* rotate 3 degrees at a time */
#define ROT_2  INT_TO_FIXED(1)  /* rotate 2 degrees at a time */

static RotateControl InitialRotate[NUM_CUBES] = {
   {     0, ROT_6, ROT_6},
   { ROT_3,     0, ROT_3},
   { ROT_3, ROT_3,     0},
   { ROT_3,-ROT_3,     0},
   {-ROT_3, ROT_2,     0},
   {-ROT_6,-ROT_3,     0},
   { ROT_3,     0,-ROT_6},
   {-ROT_2,     0, ROT_3},
   {-ROT_3,     0,-ROT_3},
   {     0, ROT_2,-ROT_2},
   {     0,-ROT_3, ROT_3},
   { ROT_2, ROT_2, ROT_2} };

const Fixedpoint minX = -200;
const Fixedpoint maxX =  200;

const Fixedpoint minY = -100;
const Fixedpoint maxY =  100;

const Fixedpoint minZ = -1100;
const Fixedpoint maxZ =  -350;

//static MoveControl InitialMove = {2,1,10, minX,minY,minZ, maxX,maxY,maxZ};
static MoveControl InitialMove = {0,0,0, minX,minY,minZ, maxX,maxY,maxZ};

/* starting coordinates for cubes in world space */
static int CubeStartCoords[NUM_CUBES][3] = {
   {100,  0,-350},
   {100, 70,-350},
   {100,-70,-350},

   { 33,  0,-350},
   { 33, 70,-350},
   { 33,-70,-350},
   {-33,  0,-350},
   {-33, 70,-350},
   {-33,-70,-350},

   {-100,  0,-350},
   {-100, 70,-350},
   {-100,-70,-350}};

/* delay counts (speed control) for cubes */
static int InitRDelayCounts[NUM_CUBES] = {1,2,1,2,1,1,1,1,1,2,1,1};
static int BaseRDelayCounts[NUM_CUBES] = {4,8,4,8,8,4,4,4,8,8,8,4};
static int InitMDelayCounts[NUM_CUBES] = {1,1,1,1,1,1,1,1,1,1,1,1};
static int BaseMDelayCounts[NUM_CUBES] = {9,9,9,9,9,9,9,9,9,9,9,9};

void InitializeCubes()
{
   int i, j, k;
   PObject *WorkingCube;

   for (i=0; i < NUM_CUBES; i++) {
      if ((WorkingCube = (PObject*)calloc(1, sizeof(PObject))) == NULL)
      {
         printf("Couldn't get memory\n");
         exit(1);
      }

      WorkingCube->DrawFunc    = DrawPObject;
      WorkingCube->RecalcFunc  = XformAndProjectPObject;
      WorkingCube->MoveFunc    = RotateAndMovePObject;
      WorkingCube->RecalcXform = 1;

      WorkingCube->RDelayCount     = InitRDelayCounts[i];
      WorkingCube->RDelayCountBase = BaseRDelayCounts[i];

      WorkingCube->MDelayCount     = InitMDelayCounts[i];
      WorkingCube->MDelayCountBase = BaseMDelayCounts[i];

      /* Set the object->world xform to none */
      for (j=0; j<3; j++)
         for (k=0; k<4; k++)
            WorkingCube->XformToWorld[j][k] = INT_TO_FIXED(0);

      WorkingCube->XformToWorld[0][0] =
         WorkingCube->XformToWorld[1][1] =
         WorkingCube->XformToWorld[2][2] = INT_TO_FIXED(1);

      /* Set the initial location */
      for (j=0; j<3; j++)
      {
          WorkingCube->XformToWorld[j][3] = INT_TO_FIXED(CubeStartCoords[i][j]);
      }

      if (i < NUM_CUBES - 1)
      {
          WorkingCube->NumVerts   = NUM_CUBE_VERTS;
          WorkingCube->VertexList = CubeVerts;
          WorkingCube->NumFaces   = NUM_CUBE_FACES;
          WorkingCube->Rotate     = InitialRotate[i];
          WorkingCube->Move.MoveX = INT_TO_FIXED(InitialMove.MoveX);
          WorkingCube->Move.MoveY = INT_TO_FIXED(InitialMove.MoveY);
          WorkingCube->Move.MoveZ = INT_TO_FIXED(InitialMove.MoveZ);

          WorkingCube->Move.MinX  = INT_TO_FIXED(InitialMove.MinX);
          WorkingCube->Move.MinY  = INT_TO_FIXED(InitialMove.MinY);
          WorkingCube->Move.MinZ  = INT_TO_FIXED(InitialMove.MinZ);

          WorkingCube->Move.MaxX  = INT_TO_FIXED(InitialMove.MaxX);
          WorkingCube->Move.MaxY  = INT_TO_FIXED(InitialMove.MaxY);
          WorkingCube->Move.MaxZ  = INT_TO_FIXED(InitialMove.MaxZ);

          WorkingCube->XformedVertexList   = (Point3*)malloc(NUM_CUBE_VERTS*sizeof(Point3));
          WorkingCube->ProjectedVertexList = (Point3*)malloc(NUM_CUBE_VERTS*sizeof(Point3));
          WorkingCube->ScreenVertexList    = (Point* )malloc(NUM_CUBE_VERTS*sizeof(Point ));
          WorkingCube->FaceList            = (Face*  )malloc(NUM_CUBE_FACES*sizeof(Face  ));

          /* Initialize object faces */
          for (j=0; j < NUM_CUBE_FACES; j++) {
             WorkingCube->FaceList[j].VertNums = VertNumList[j];
             WorkingCube->FaceList[j].NumVerts = VertsInFace[j];
             WorkingCube->FaceList[j].Color    = rand() & 0xFFu; // random colors
          }
      }
      else // else setup the cow object
      {
          int32_t NumVerts = ARRAYSIZE(cow_vertices);
          int32_t NumFaces = ARRAYSIZE(cow_nvertices) / 3; // = 3156
          WorkingCube->NumVerts = NumVerts;
          WorkingCube->NumFaces = NumFaces;

          // Convert floating point vertices to fixed point
          Point3* vertices = (Point3*)malloc(NumVerts * sizeof(Point3));
          for (j=0; j < NumVerts; j++)
          {
              vertices[j].X = DOUBLE_TO_FIXED(cow_vertices[j].X * 5.0);
              vertices[j].Y = DOUBLE_TO_FIXED(cow_vertices[j].Y * 5.0);
              vertices[j].Z = DOUBLE_TO_FIXED(cow_vertices[j].Z * 5.0);
          }

          WorkingCube->VertexList = vertices; // point Point3* to array of Point3

          WorkingCube->Rotate     = InitialRotate[i];
          WorkingCube->Move.MoveX = INT_TO_FIXED(InitialMove.MoveX);
          WorkingCube->Move.MoveY = INT_TO_FIXED(InitialMove.MoveY);
          WorkingCube->Move.MoveZ = INT_TO_FIXED(InitialMove.MoveZ);

          WorkingCube->Move.MinX  = INT_TO_FIXED(InitialMove.MinX);
          WorkingCube->Move.MinY  = INT_TO_FIXED(InitialMove.MinY);
          WorkingCube->Move.MinZ  = INT_TO_FIXED(InitialMove.MinZ);

          WorkingCube->Move.MaxX  = INT_TO_FIXED(InitialMove.MaxX);
          WorkingCube->Move.MaxY  = INT_TO_FIXED(InitialMove.MaxY);
          WorkingCube->Move.MaxZ  = INT_TO_FIXED(InitialMove.MaxZ);

          WorkingCube->XformedVertexList   = (Point3*)malloc(NumVerts*sizeof(Point3));
          WorkingCube->ProjectedVertexList = (Point3*)malloc(NumVerts*sizeof(Point3));
          WorkingCube->ScreenVertexList    = (Point* )malloc(NumVerts*sizeof(Point ));
          WorkingCube->FaceList            = (Face*  )malloc(NumFaces*sizeof(Face  ));

          // Initialize the object faces
          for (j=0; j < NumFaces; j++) {
             WorkingCube->FaceList[j].VertNums = &cow_nvertices[j * 3];
             WorkingCube->FaceList[j].NumVerts = 3;
             WorkingCube->FaceList[j].Color    = rand() & 0xFFu; // random colors
          }
      }
      ObjectList[NumObjects++] = WorkingCube;
   }
}

////////////////////////////////////////////////////////////////////////////////
void Render(
    PObject* ObjectList[NUM_CUBES],
    Canvas&  canvas,
    bool     enableGrid = false)
{
    Fixedpoint nearClipZ = DOUBLE_TO_FIXED(-2.0);

    // For each object, update position and orientation
    int i;
    for (i=0; i < NumObjects; i++) {
       if (ObjectList[i]->RecalcXform || RecalcAllXforms) {
          ObjectList[i]->RecalcFunc(ObjectList[i], &canvas, nearClipZ);
          ObjectList[i]->RecalcXform = 0;
       }
    }
    RecalcAllXforms = 0; // disable 1-shot start-up recalculating

    // Draw all objects to framebuffer
    canvas.SetCanvas(0u); // clear prior to render
    for (i=0; i < NumObjects; i++)
    {
        ObjectList[i]->DrawFunc(ObjectList[i], &canvas);
    }

    // Move and reorient each object for next iteration
    for (i=0; i < NumObjects; i++) { ObjectList[i]->MoveFunc(ObjectList[i]); }

    if (enableGrid)
    {
        // Define thin rectangle for use as a line to draw a grid
        const int32_t lineWidth = 1;
        const int width  = canvas.Width();
        const int height = canvas.Height();
        Point horzLine[] = {{0,0}, {width-1, 0},
                                   {width-1, lineWidth}, {0, lineWidth}};
        Point vertLine[] = {{0,0}, {lineWidth,              0},
                                   {lineWidth, height-1}, {0, height-1}};
        assert(ARRAYSIZE(horzLine) == 4);

        // Draw grid lines to show lens distortion
        for (i = 0; i < 7; ++i)
        {
            FillConvexPolygon(horzLine, ARRAYSIZE(horzLine), 200, 0, i * (height - 1) / 6, &canvas);
        }
        for (i = 0; i < 10; ++i)
        {
            FillConvexPolygon(vertLine, ARRAYSIZE(vertLine), 200, i * (width - 1) / 9, 0, &canvas);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
// Open a debug console for printf output
DWORD OpenConsole(const wchar_t* title)
{
    if (AllocConsole() == 0) { return GetLastError(); }

    SetConsoleTitle(title);

    // Close FILE associated with stdout and reassign stdout to "CONOUT$"
    FILE* stream = NULL; // dummy argument
    if (freopen_s(&stream, "CONOUT$", "w", stdout) != 0) { return -1; }

    return 0;
}

////////////////////////////////////////////////////////////////////////////////
int WINAPI wWinMain(
    HINSTANCE hInstance,
    HINSTANCE hPrevInstance,
    PWSTR     pCmdLine,
    int       nCmdShow)
{
    const int width  = 1008; // TODO: make command line arguments
    const int height =  768;

    // open console for printf output
#ifdef DEBUG_CONSOLE
    if (OpenConsole(L"CubeTest Console") != 0) { return 0; }
#endif

    // uint32_t Canvas that will contain 2D rendering of 3D scene
    // TODO: uint16_t would be faster since half the data movement (but can
    // only support up to 65k photons).
    Canvas32 renderCanvas(width, height); // 3D to 2D rendering
    SpadSim       spadSim(width, height); // lens distortion, dark frame, noise, etc
    GdiWindow      window(width, height); // GUI window to display final image

    // intentionally making window bigger as we don't get the size we ask for,
    if (!window.Create(L"CubeTest", WS_OVERLAPPEDWINDOW, 0, 0, 0, width + 64, height + 64))
    {
        return 0;
    }
    ShowWindow(window.Window(), nCmdShow);

    InitializeFixedPoint(); /* set up fixed-point data */
    InitializeCubes();      /* set up cubes and add them to object list; other
                               objects would be initialized now, if there were any */

    auto t_start = std::chrono::high_resolution_clock::now();
    uint32_t frameCount = 0;
    double fps = 0.0;
    std::string fpsStr = "FPS = " + std::to_string(fps);
    const uint32_t* pRd = (const uint32_t*)renderCanvas.GetFrameBuffer();
    uint32_t*       pWr = (      uint32_t*)window.GetFrameBuffer();
    while (window.Update()) // process events, display framebuffer
    {
        // Note: 3D to 2D rendering is mostly sensor independent with exception
        // of the exposure characteristics (e.g. exposure time, number of lines
        // that expose simultaneously, read-out time, etc).
        // TODO: add sensor characteristic arguments to Render()
        Render(ObjectList, renderCanvas, true);

        // simulate lens and sensor
        bool enableLensDist = true;
        bool enableDF       = true;
        bool enablePWL      = false;
        spadSim.AddDistortion(pRd, pWr, enableLensDist, enableDF, enablePWL);

        // write fps to window, must be done every frame
        window.SetText(fpsStr, 50, 50, 0x00000000u);

        // Update fps every 64 frames
        if ((++frameCount & 63) == 0)
        {
            auto t_end = std::chrono::high_resolution_clock::now();
            auto passedTimeMs = std::chrono::duration<double, std::milli>(t_end - t_start).count();
            fps = frameCount / (passedTimeMs / 1000.0);
            fpsStr = "FPS = " + std::to_string(fps);

            // set up for next set of frames
            frameCount = 0;
            t_start = std::chrono::high_resolution_clock::now();
        }
    }

    // free ObjectList
    for (int i = 0; i < NumObjects; ++i)
    {
        // TODO: switch to use std::vector rather than malloc/free
        free(ObjectList[i]);
        ObjectList[i] = nullptr;
    }

#ifdef DEBUG_CONSOLE
    FreeConsole(); // needed due to AllocConsole()
#endif
    return 0;
}
