// Fixed point 3D to 2D rendering code adapted from Michael Abrash's excellent
// book:
// https://github.com/jagregory/abrash-black-book
// https://www.jagregory.com/abrash-black-book/
// This is HW independent vanilla code that does not use doubles or floats.
// Instead int32_t are used with 16 fractional bits so it works on CPU's
// without FPU (like RP2040).
//
// TODO: add z-buffer or back-to-front rendering of objects
// TODO: add (U,V) coordinates to vertices for texture mapping

#pragma once

#ifndef __RenderFXP_h__
#define __RenderFXP_h__

#include <stdint.h> // int32_t, etc
#include "Canvas.h" // Canvas class to abstract pixel type (e.g. RGB, grayscale, etc)

// Maximum screen height in pixels that is supported
// (arbitrary, OK to modify this)
#define MAX_SCREEN_HEIGHT 2048u

#define FIXED_FBITS        (16u) // fractional bits in Fixedpoint number
#define FIXED_ONE          (1 <<  FIXED_FBITS)      // 1.0
#define FIXED_HALF         (1 << (FIXED_FBITS - 1)) // 0.5, for rounding
#define FIXED_FBITS_MASK   (FIXED_ONE - 1)
#define INT_TO_FIXED(x)    (((Fixedpoint)x) << FIXED_FBITS)
#define FIXED_TO_INT(x)    ((int)(((x) + FIXED_HALF) >> FIXED_FBITS))
#define DOUBLE_TO_FIXED(x) ((Fixedpoint)(x * (1 << FIXED_FBITS) + 0.5))
#define FIXED_TO_DOUBLE(x) (x / (double)FIXED_ONE)

// arbitrary limit to vertices per polygon, can safely increase this
// so long as polygons remain convex.
#define MAX_POLY_LENGTH 6

// Q16.16: 1 sign bit, (31 - FIXED_FBITS) integer and FIXED_FBITS fractional bits
typedef int32_t Fixedpoint;

// A 4x4 matrix where last row is assumed to be {0, 0, 0, 1}
typedef Fixedpoint Xform[3][4];

// Describes a single 2D point
typedef struct { int32_t X; int32_t Y; } Point;

// Describes a single 3D point in homogeneous coordinates; the W
// coordinate isn't present, though; assumed to be 1 and implied
typedef struct { Fixedpoint X, Y, Z; } Point3;
typedef struct { int32_t    X, Y, Z; } IntPoint3;

// structure describing one face of an object.
// Each face is a convex polygon where each vertex is connected to adjacent
// vertices and last vertex is assumed connected to the first.
typedef struct { int32_t* VertNums; int32_t NumVerts; int32_t Color; } Face;

// Rotation increments in degrees
typedef struct { Fixedpoint RotateX, RotateY, RotateZ; } RotateControl;

// X,Y,Z increments and position bounding box
typedef struct {
   Fixedpoint MoveX, MoveY, MoveZ;
   Fixedpoint MinX, MinY, MinZ;
   Fixedpoint MaxX, MaxY, MaxZ;
} MoveControl;

// structure describing a polygon-based object
typedef struct t_PObject PObject;

struct t_PObject {
   // fields common to every object
   void          (*RecalcFunc)(PObject*, Canvas*, Fixedpoint); // transform object vertices
   void          (*DrawFunc)  (PObject*, Canvas*); // draw object to canvas
   void          (*MoveFunc)  (PObject*);          // move/rotate object, set RecalcXform
   int32_t       RecalcXform;                      // 1 to flag need to call RecalcFunc

   MoveControl   Move;                // X,Y,Z increments and position bounding box
   int32_t       MDelayCount;         // move when this count reaches zero
   int32_t       MDelayCountBase;     // reset value of MDelayCount

   RotateControl Rotate;              // controls rotation change over time
   int32_t       RDelayCount;         // rotate when this count reaches zero
   int32_t       RDelayCountBase;     // reset value of RDelayCount

   Xform         XformToWorld;        // xform from object->world space
   Xform         XformToView;         // xform from object->view space

   int32_t       NumVerts;            // # vertices in VertexList
   Point3*       VertexList;          // vertices in object space
   Point3*       XformedVertexList;   // xformed into view space
   Point3*       ProjectedVertexList; // projected into screen space
   Point*        ScreenVertexList;    // converted to screen coordinates
   int32_t       NumFaces;            // # of faces in object (# of polygons)
   Face*         FaceList;            // pointer to face info
};

////////////////////////////////////////////////////////////////////////////////
// Multiply and divide operations for Fixedpoint.
// FixedDiv() does "den = (den == 0) ? 1 : den" to avoid div-by-0
Fixedpoint FixedMul(Fixedpoint   a, Fixedpoint   b); // result = a * b
Fixedpoint FixedDiv(Fixedpoint num, Fixedpoint den); // result = num / den

////////////////////////////////////////////////////////////////////////////////
/* Matrix multiplies Xform by SourceVec, and stores the result in DestVec.
   Multiplies a 4x4 matrix times a 4x1 matrix; the result is a 4x1 matrix.
   Cheats by assuming W coord is 1 and bottom row of matrix is 0 0 0 1,
   and doesn't bother to set the W coordinate of the destination. */
/* Example:
   [dst1]   [a b c d]   [src1]
   [dst2] = [e f g h] * [src2]
   [dst3]   [i j k l]   [src3]
   [ 1  ]   [0 0 0 1]   [  1 ] */
void XformVec(Xform xform3x4, Fixedpoint* SrcVec3x1, Fixedpoint* DstVec3x1);

////////////////////////////////////////////////////////////////////////////////
/* Matrix multiplies Src1 by Src2 and stores result in Dest.
   Cheats by assuming bottom row of each matrix is 0 0 0 1, and doesn't bother
   to set the bottom row of the destination. */
void ConcatXforms(Xform Src1, Xform Src2, Xform Dest);

////////////////////////////////////////////////////////////////////////////////
/* Color-fills a convex 2D polygon. All vertices are offset by (XOffset,
  YOffset). "Convex" means that every horizontal line drawn through
  the polygon at any point would cross exactly two active edges
  (neither horizontal lines nor zero-length edges count as active
  edges; both are acceptable anywhere in the polygon), and that the
  right & left edges never cross. (It's OK for them to touch, though,
  so long as the right edge never crosses over to the left of the
  left edge.) Nonconvex polygons won't be drawn properly. Returns 1
  for success, 0 if memory allocation failed. */
int32_t FillConvexPolygon(
    Point * PointPtr,
    int32_t Length,
    int32_t color,
    int32_t XOffset, int32_t YOffset,
    Canvas* pCanvas);

////////////////////////////////////////////////////////////////////////////////
/* Draws all visible faces in specified polygon-based object. Object must have
   previously been transformed and projected, so that ScreenVertexList array is
   filled in. */
void DrawPObject(PObject *, Canvas*);            // DrawFunc

////////////////////////////////////////////////////////////////////////////////
/* Transforms all vertices in the specified polygon-based object into view
   space, then perspective projects them to screen space and maps them to screen
   coordinates, storing results in the object. Recalculates object->view
   transformation because only if transform changes would we bother
   to retransform the vertices.
   nearClipZ is distance from viewpoint to projection plane (usually -1.0)
*/
void XformAndProjectPObject(PObject *, Canvas*, Fixedpoint nearClipZ); // RecalcFunc

////////////////////////////////////////////////////////////////////////////////
 /* Rotates and moves a polygon-based object around the three axes.
   Movement is implemented only along the Z axis currently. */
void RotateAndMovePObject(PObject *);            // MoveFunc

////////////////////////////////////////////////////////////////////////////////
/* Concatenate a rotation by Angle around the X, Y or Z axis to transformation
   in Xform, placing the result back into Xform. */
void AppendRotationX(Xform, Fixedpoint);
void AppendRotationY(Xform, Fixedpoint);
void AppendRotationZ(Xform, Fixedpoint);

////////////////////////////////////////////////////////////////////////////////
// LUT based function to return the cosine and sin of an angle.
// Maximum absolute error is 0.000054 over all possible input degrees
// (when FIXED_FBITS = 16)
void CosSin(
    Fixedpoint  degrees, //  in: angle in degrees
    Fixedpoint* pCos,    // out: cosine of angle
    Fixedpoint* pSin);   // out:    sin of angle

////////////////////////////////////////////////////////////////////////////////
// Return tan(degrees)
Fixedpoint tanFixed(Fixedpoint degrees);

#endif // #ifndef __RenderFXP_h__
