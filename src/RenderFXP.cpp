#include <assert.h>

#include "RenderFXP.h"

#define SWAP(x, y) { \
    int tmp = x; \
    x = y; \
    y = tmp; }

#define ABS(x) (((x) < 0) ? (-(x)) : (x))

////////////////////////////////////////////////////////////////////////////////
// typedefs for usage internal to this file

// Describes beginning and ending X coordinates of a single horizontal line
typedef struct { int32_t XStart; int32_t XEnd; } HLine;

/* Describes a Length-long series of horizontal lines, all assumed to
   be on contiguous scan lines starting at YStart and proceeding
   downward (used to describe a scan-converted polygon to the
   low-level hardware-dependent drawing code). */
typedef struct {
    int32_t Length;
    int32_t YStart;
    HLine   HLinePtr[MAX_SCREEN_HEIGHT];
} HLineList;

////////////////////////////////////////////////////////////////////////////////
Fixedpoint FixedMul(Fixedpoint M1, Fixedpoint M2)
{
    // TODO: rounding
    // Note: downshift of a signed value is considered undefined so
    // we use an actual divide here. The compiler should replace with a shift.
    return (Fixedpoint)(((int64_t)M1 * (int64_t)M2) / (1 << FIXED_FBITS));
}

////////////////////////////////////////////////////////////////////////////////
Fixedpoint FixedDiv(Fixedpoint M1, Fixedpoint M2)
{
    // TODO: rounding
    if (M2 == 0) { M2 = 1; } // avoid div-by-0
    return (Fixedpoint)(((int64_t)M1 << FIXED_FBITS) / (int64_t)M2);
}

////////////////////////////////////////////////////////////////////////////////
/* Matrix multiplies Xform by SourceVec, and stores the result in DestVec.
   Multiplies a 4x4 matrix times a 4x1 matrix; the result is a 4x1 matrix. Cheats
   by assuming the W coord is 1 and bottom row of matrix is 0 0 0 1, and doesn't
   bother to set the W coordinate of the destination. */
/* Example:
   [dst1]   [a b c d]   [src1]
   [dst2] = [e f g h] * [src2]
   [dst3]   [i j k l]   [src3]
   [ 1  ]   [0 0 0 1]   [  1 ] */
void XformVec(
    Xform       WorkingXform,
    Fixedpoint* SourceVec,
    Fixedpoint* DestVec)
{
   for (int i=0; i < 3; i++)
   {
      DestVec[i] = FixedMul(WorkingXform[i][0],      SourceVec[0]) +
                   FixedMul(WorkingXform[i][1],      SourceVec[1]) +
                   FixedMul(WorkingXform[i][2],      SourceVec[2]) +
                            WorkingXform[i][3]; // * SourceVec[3] = W = 1.0
   }
}

////////////////////////////////////////////////////////////////////////////////
/* Matrix multiplies SourceXform1 by SourceXform2 and stores result in
   DestXform.
   Cheats by assuming bottom row of each matrix is 0 0 0 1, and doesn't bother
   to set the bottom row of the destination. */
/* Example:
   DestXform     = SourceXform1  * SourceXform2

   [da db dc dd]   [a1 b1 c1 d1]   [a2 b2 c2 d2]
   [de df df dh] = [e1 f1 g1 h1] * [e2 f2 g2 h2]
   [di dj dk dl]   [i1 j1 k1 l1]   [i2 j2 k2 l2]
   [ 0  0  0  1]   [ 0  0  0  1]   [ 0  0  0  1] */
void ConcatXforms(
   Xform SourceXform1,
   Xform SourceXform2,
   Xform DestXform)
{
   for (int i=0; i < 3; i++)    // loop thru rows of DestXform
   {
      for (int j=0; j < 4; j++) // loop thru columns of DestXForm
      {
         DestXform[i][j] =
               FixedMul(SourceXform1[i][0],      SourceXform2[0][j]) +
               FixedMul(SourceXform1[i][1],      SourceXform2[1][j]) +
               FixedMul(SourceXform1[i][2],      SourceXform2[2][j]) +
                        SourceXform1[i][3]; // * SourceXform2[3][3] = 1
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
/* Transforms all vertices in the specified polygon-based object into view
   space, then perspective projects them to screen space and maps them to screen
   coordinates, storing results in the object. Recalculates object->view
   transformation because only if transform changes would we bother
   to retransform the vertices. */

void XformAndProjectPObject(
    PObject*   ObjectToXform,
    Canvas*    pCanvas,
    Fixedpoint nearClipZ) // Z-distance from viewpoint to projection plane
                          // (this is usually set to -1.0)
{
   const int widthDiv2  = pCanvas->Width()  / 2;
   const int heightDiv2 = pCanvas->Height() / 2;

   // Recalculate the object->view transform
   // TODO: pass in WorldViewXform to this function
   Xform WorldViewXform = {{INT_TO_FIXED(1),0,0,0},
                           {0,INT_TO_FIXED(1),0,0},
                           {0,0,INT_TO_FIXED(1),0}};
   ConcatXforms(WorldViewXform,               // Src1
                ObjectToXform->XformToWorld,  // Src2
                ObjectToXform->XformToView);  // Dst

   // Apply new transformation and project the points
   const Fixedpoint tmp = FixedMul(nearClipZ, INT_TO_FIXED(widthDiv2));
   int NumPoints      = ObjectToXform->NumVerts;
   Point3* Pts        = ObjectToXform->VertexList;
   Point3* XformedPts = ObjectToXform->XformedVertexList;
   Point3* ProjPts    = ObjectToXform->ProjectedVertexList;
   Point*  ScreenPts  = ObjectToXform->ScreenVertexList;
   for (int i=0; i < NumPoints; i++, Pts++, XformedPts++, ProjPts++, ScreenPts++)
   {
      // xform from world to view coordinates
      XformVec(ObjectToXform->XformToView,
              (Fixedpoint*)Pts,
              (Fixedpoint*)XformedPts);

      // Perspective-project from view to projection plane:
      //     projX = viewX / viewZ * (nearClipZ * width/2)
      ProjPts->X = FixedMul(FixedDiv(XformedPts->X, XformedPts->Z), tmp);
      ProjPts->Y = FixedMul(FixedDiv(XformedPts->Y, XformedPts->Z), tmp);
      ProjPts->Z = XformedPts->Z;

      // Convert projection plane to screen coordinates.
      // The Y coord is negated to flip from increasing Y being up to increasing
      // Y being down, as expected by FillConvexPolygon.
      // Add in half the screen width and height to center on screen.
      ScreenPts->X =  FIXED_TO_INT(ProjPts->X) + widthDiv2;
      ScreenPts->Y = -FIXED_TO_INT(ProjPts->Y) + heightDiv2;
   }
}

////////////////////////////////////////////////////////////////////////////////
// Helper function for use in CosSin().
// Returns cos(x) for an angle in 0 <= x <= 90
// Maximum absolute error is 0.000054 over all possible input degrees.
static Fixedpoint cos90(Fixedpoint degrees)
{
    assert((0 <= degrees) && (degrees <= INT_TO_FIXED(90)));

    // LUT has entry every degree.
    // Each entry has an offset and slope.
    /* python code to generate LUT:
import math
lutBits = 16
for i in range(91):
    cosL = cos( i      * pi / 180.0)
    cosR = cos((i + 1) * pi / 180.0)
    deltaY = cosL - cosR;
    offset = round(cosL   * (1 << lutBits))
    slope  = round(deltaY * (1 << lutBits))
    print(f"{{ {offset}, {slope} }},")
    */
    typedef struct {
        uint16_t offset;
        uint16_t slope;
    } lutEntry;

    const lutEntry cosLut[91] = {
        { 65535, 10 }, { 65526, 30 }, { 65496, 50 }, { 65446, 70 }, { 65376, 90 },
        { 65287, 110 }, { 65177, 129 }, { 65048, 149 }, { 64898, 169 }, { 64729, 189 },
        { 64540, 208 }, { 64332, 228 }, { 64104, 248 }, { 63856, 267 }, { 63589, 286 },
        { 63303, 306 }, { 62997, 325 }, { 62672, 344 }, { 62328, 363 }, { 61966, 382 },
        { 61584, 401 }, { 61183, 419 }, { 60764, 438 }, { 60326, 456 }, { 59870, 474 },
        { 59396, 492 }, { 58903, 510 }, { 58393, 528 }, { 57865, 546 }, { 57319, 563 },
        { 56756, 581 }, { 56175, 598 }, { 55578, 615 }, { 54963, 631 }, { 54332, 648 },
        { 53684, 664 }, { 53020, 680 }, { 52339, 696 }, { 51643, 712 }, { 50931, 728 },
        { 50203, 743 }, { 49461, 758 }, { 48703, 773 }, { 47930, 787 }, { 47143, 802 },
        { 46341, 816 }, { 45525, 830 }, { 44695, 843 }, { 43852, 857 }, { 42995, 870 },
        { 42126, 883 }, { 41243, 895 }, { 40348, 907 }, { 39441, 919 }, { 38521, 931 },
        { 37590, 943 }, { 36647, 954 }, { 35693, 965 }, { 34729, 975 }, { 33754, 986 },
        { 32768, 996 }, { 31772, 1005 }, { 30767, 1015 }, { 29753, 1024 }, { 28729, 1032 },
        { 27697, 1041 }, { 26656, 1049 }, { 25607, 1057 }, { 24550, 1064 }, { 23486, 1071 },
        { 22415, 1078 }, { 21336, 1085 }, { 20252, 1091 }, { 19161, 1097 }, { 18064, 1102 },
        { 16962, 1107 }, { 15855, 1112 }, { 14742, 1117 }, { 13626, 1121 }, { 12505, 1125 },
        { 11380, 1128 }, { 10252, 1131 }, { 9121, 1134 }, { 7987, 1136 }, { 6850, 1139 },
        { 5712, 1140 }, { 4572, 1142 }, { 3430, 1143 }, { 2287, 1143 }, { 1144, 1144 },
        { 0, 1144 }
    };

    // Linearly interpolate LUT
    const int   idx = degrees >> FIXED_FBITS;     // get integer part of degrees
    const int fbits = degrees & FIXED_FBITS_MASK; // get fractional bits for lerp

    // Note: not using Fixed_Mult() as we don't need the casts to int64_t
    return (Fixedpoint)((int)cosLut[idx].offset -
                        ((fbits * cosLut[idx].slope) >> FIXED_FBITS));
}

////////////////////////////////////////////////////////////////////////////////
// LUT based function to return the cosine and sin of an angle
void CosSin(
    Fixedpoint  degrees, //  in: angle in degrees
    Fixedpoint* pCos,    // out: cosine of angle
    Fixedpoint* pSin)    // out:    sin of angle
{
    // ensure 0 <= degrees <= 360
    const Fixedpoint deg360 = INT_TO_FIXED(360);
    while (degrees <      0) { degrees += deg360; } // make angle positive
    while (degrees > deg360) { degrees -= deg360; } // make angle degrees <= 360

    if      (degrees <= INT_TO_FIXED(90)) // quadrant 0: 0 <= degrees <= 90
    {
        *pCos = cos90(degrees);
        *pSin = cos90(INT_TO_FIXED(90) - degrees);
    }
    else if (degrees <= INT_TO_FIXED(180)) // quadrant 1: 90 < degrees <= 180
    {
        degrees = INT_TO_FIXED(180) - degrees; // makes 0 <= degrees < 90
        *pCos = -cos90(degrees);
        *pSin = cos90(INT_TO_FIXED(90) - degrees);
    }
    else if (degrees <= INT_TO_FIXED(270)) // quadrant 2: 180 < degrees <= 270
    {
        degrees -= INT_TO_FIXED(180);
        *pCos = -cos90(degrees);
        *pSin = -cos90(INT_TO_FIXED(90) - degrees);
    }
    else                                   // quadrant 3: 270 < degrees < 360
    {
        degrees = INT_TO_FIXED(360) - degrees; // makes 0 <= degrees < 90
        *pCos = cos90(degrees);
        *pSin = -cos90(INT_TO_FIXED(90) - degrees);
    }
}

////////////////////////////////////////////////////////////////////////////////
// return tan() of an angle
Fixedpoint tanFixed(Fixedpoint degrees)
{
   Fixedpoint CosTemp, SinTemp;
   CosSin(degrees, &CosTemp, &SinTemp);

   return FixedDiv(CosTemp, SinTemp);
}

////////////////////////////////////////////////////////////////////////////////
/* Concatenate a rotation by Angle around the X axis to transformation in
   XformToChange, placing the result back into XformToChange. */
void AppendRotationX(Xform XformToChange, Fixedpoint Angle)
{
   Fixedpoint Temp10, Temp11, Temp12, Temp20, Temp21, Temp22;
   Fixedpoint CosTemp, SinTemp;
   CosSin(Angle, &CosTemp, &SinTemp);

   /* Calculate the new values of the six affected matrix entries */
   Temp10 = FixedMul(CosTemp, XformToChange[1][0]) +
           FixedMul(-SinTemp, XformToChange[2][0]);
   Temp11 = FixedMul(CosTemp, XformToChange[1][1]) +
           FixedMul(-SinTemp, XformToChange[2][1]);
   Temp12 = FixedMul(CosTemp, XformToChange[1][2]) +
           FixedMul(-SinTemp, XformToChange[2][2]);

   Temp20 = FixedMul(SinTemp, XformToChange[1][0]) +
            FixedMul(CosTemp, XformToChange[2][0]);
   Temp21 = FixedMul(SinTemp, XformToChange[1][1]) +
            FixedMul(CosTemp, XformToChange[2][1]);
   Temp22 = FixedMul(SinTemp, XformToChange[1][2]) +
            FixedMul(CosTemp, XformToChange[2][2]);
   /* Put the results back into XformToChange */
   XformToChange[1][0] = Temp10; XformToChange[1][1] = Temp11;
   XformToChange[1][2] = Temp12; XformToChange[2][0] = Temp20;
   XformToChange[2][1] = Temp21; XformToChange[2][2] = Temp22;
}

////////////////////////////////////////////////////////////////////////////////
/* Concatenate a rotation by Angle around the Y axis to transformation in
   XformToChange, placing the result back into XformToChange. */
void AppendRotationY(Xform XformToChange, Fixedpoint Angle)
{
   Fixedpoint Temp00, Temp01, Temp02, Temp20, Temp21, Temp22;
   Fixedpoint CosTemp, SinTemp;
   CosSin(Angle, &CosTemp, &SinTemp);

   /* Calculate the new values of the six affected matrix entries */
   Temp00 = FixedMul(CosTemp, XformToChange[0][0]) +
            FixedMul(SinTemp, XformToChange[2][0]);
   Temp01 = FixedMul(CosTemp, XformToChange[0][1]) +
            FixedMul(SinTemp, XformToChange[2][1]);
   Temp02 = FixedMul(CosTemp, XformToChange[0][2]) +
            FixedMul(SinTemp, XformToChange[2][2]);

   Temp20 = FixedMul(-SinTemp, XformToChange[0][0]) +
            FixedMul( CosTemp, XformToChange[2][0]);
   Temp21 = FixedMul(-SinTemp, XformToChange[0][1]) +
             FixedMul(CosTemp, XformToChange[2][1]);
   Temp22 = FixedMul(-SinTemp, XformToChange[0][2]) +
             FixedMul(CosTemp, XformToChange[2][2]);
   /* Put the results back into XformToChange */
   XformToChange[0][0] = Temp00; XformToChange[0][1] = Temp01;
   XformToChange[0][2] = Temp02; XformToChange[2][0] = Temp20;
   XformToChange[2][1] = Temp21; XformToChange[2][2] = Temp22;
}

////////////////////////////////////////////////////////////////////////////////
/* Concatenate a rotation by Angle around the Z axis to transformation in
   XformToChange, placing the result back into XformToChange. */
void AppendRotationZ(Xform XformToChange, Fixedpoint Angle)
{
   Fixedpoint Temp00, Temp01, Temp02, Temp10, Temp11, Temp12;
   Fixedpoint CosTemp, SinTemp;
   CosSin(Angle, &CosTemp, &SinTemp);

   /* Calculate the new values of the six affected matrix entries */
   Temp00 = FixedMul(CosTemp, XformToChange[0][0]) +
           FixedMul(-SinTemp, XformToChange[1][0]);
   Temp01 = FixedMul(CosTemp, XformToChange[0][1]) +
           FixedMul(-SinTemp, XformToChange[1][1]);
   Temp02 = FixedMul(CosTemp, XformToChange[0][2]) +
           FixedMul(-SinTemp, XformToChange[1][2]);

   Temp10 = FixedMul(SinTemp, XformToChange[0][0]) +
            FixedMul(CosTemp, XformToChange[1][0]);
   Temp11 = FixedMul(SinTemp, XformToChange[0][1]) +
            FixedMul(CosTemp, XformToChange[1][1]);
   Temp12 = FixedMul(SinTemp, XformToChange[0][2]) +
            FixedMul(CosTemp, XformToChange[1][2]);
   /* Put the results back into XformToChange */
   XformToChange[0][0] = Temp00; XformToChange[0][1] = Temp01;
   XformToChange[0][2] = Temp02; XformToChange[1][0] = Temp10;
   XformToChange[1][1] = Temp11; XformToChange[1][2] = Temp12;
}

////////////////////////////////////////////////////////////////////////////////
// Rotates and moves a polygon-based object around the three axes.
void RotateAndMovePObject(PObject * ObjectToMove)
{
    if (ObjectToMove->RDelayCount-- == 0) {   // rotate
        ObjectToMove->RDelayCount = ObjectToMove->RDelayCountBase;

        if (ObjectToMove->Rotate.RotateX != 0)
         AppendRotationX(ObjectToMove->XformToWorld, ObjectToMove->Rotate.RotateX);

        if (ObjectToMove->Rotate.RotateY != 0)
         AppendRotationY(ObjectToMove->XformToWorld, ObjectToMove->Rotate.RotateY);

        if (ObjectToMove->Rotate.RotateZ != 0)
         AppendRotationZ(ObjectToMove->XformToWorld, ObjectToMove->Rotate.RotateZ);

        ObjectToMove->RecalcXform = 1;
    }

    // Move with bounce off boundries
    if (ObjectToMove->MDelayCount-- == 0) {
        ObjectToMove->MDelayCount = ObjectToMove->MDelayCountBase;

        Fixedpoint newPos = ObjectToMove->XformToWorld[0][3] + ObjectToMove->Move.MoveX;
        if (newPos > ObjectToMove->Move.MaxX) // check bounds
        {
            newPos = ObjectToMove->Move.MaxX;
            ObjectToMove->Move.MoveX = -(ObjectToMove->Move.MoveX); // bounce
        }
        if (newPos < ObjectToMove->Move.MinX)
        {
            newPos = ObjectToMove->Move.MinX;
            ObjectToMove->Move.MoveX = -(ObjectToMove->Move.MoveX); // bounce
        }
        ObjectToMove->XformToWorld[0][3] = newPos;

        newPos = ObjectToMove->XformToWorld[1][3] + ObjectToMove->Move.MoveY;
        if (newPos > ObjectToMove->Move.MaxY) // check bounds
        {
            newPos = ObjectToMove->Move.MaxY;
            ObjectToMove->Move.MoveY = -(ObjectToMove->Move.MoveY); // bounce
        }
        if (newPos < ObjectToMove->Move.MinY)
        {
            newPos = ObjectToMove->Move.MinY;
            ObjectToMove->Move.MoveY = -(ObjectToMove->Move.MoveY); // bounce
        }
        ObjectToMove->XformToWorld[1][3] = newPos;

        newPos = ObjectToMove->XformToWorld[2][3] + ObjectToMove->Move.MoveZ;
        if (newPos > ObjectToMove->Move.MaxZ) // check bounds
        {
            newPos = ObjectToMove->Move.MaxZ;
            ObjectToMove->Move.MoveZ = -(ObjectToMove->Move.MoveZ); // bounce
        }
        if (newPos < ObjectToMove->Move.MinZ)
        {
            newPos = ObjectToMove->Move.MinZ;
            ObjectToMove->Move.MoveZ = -(ObjectToMove->Move.MoveZ); // bounce
        }
        ObjectToMove->XformToWorld[2][3] = newPos;

        ObjectToMove->RecalcXform = 1;
    }
}

////////////////////////////////////////////////////////////////////////////////
/* Draws all visible faces in specified polygon-based object. Object must have
   previously been transformed and projected, so that ScreenVertexList array is
   filled in. */

void DrawPObject(
    PObject* ObjectToXform,
    Canvas*  pCanvas)
{
   Point* ScreenPoints = ObjectToXform->ScreenVertexList;

   // Draw each visible face (polygon) of the object in turn
   const int NumFaces = ObjectToXform->NumFaces;
   Face* FacePtr = ObjectToXform->FaceList;
   for (int i=0; i < NumFaces; i++, FacePtr++) {

      /* Copy face vertices from the vertex list */
      const int NumVertices = FacePtr->NumVerts;
      int*      VertNumsPtr = FacePtr->VertNums;
      assert(NumVertices <= MAX_POLY_LENGTH);
      Point Vertices[MAX_POLY_LENGTH];
      for (int j = 0; j < NumVertices; j++)
      {
         Vertices[j] = ScreenPoints[*VertNumsPtr++];
      }

      // Draw only if face normal points toward viewer (i.e. has a positive Z)
      long v1 = Vertices[            1].X - Vertices[0].X;
      long w1 = Vertices[NumVertices-1].X - Vertices[0].X;
      long v2 = Vertices[            1].Y - Vertices[0].Y;
      long w2 = Vertices[NumVertices-1].Y - Vertices[0].Y;
      if ((v1*w2 - v2*w1) > 0) { // if facing the screen, draw
         FillConvexPolygon(Vertices, NumVertices, FacePtr->Color, 0, 0, pCanvas);
      }
   }
}

////////////////////////////////////////////////////////////////////////////////
void DrawHorizontalLineList(
    const HLineList* HLineListPtr, // array of horizontal lines
    int              Color,        // TODO: remove this later
    Canvas*          pCanvas)      // ptr to function
{
    const int width  = pCanvas->Width();
    const int height = pCanvas->Height();

    // Draw each horizontal line in turn, starting with the top
    const int    YStart   = HLineListPtr->YStart;
    const int    YEnd     = HLineListPtr->YStart + HLineListPtr->Length - 1;
    const HLine* HLinePtr = HLineListPtr->HLinePtr; // first (top) horizontal line
    for (int Y = YStart; Y <= YEnd; ++Y, HLinePtr++)
    {
        // bounds check Y
        if (Y <       0) { continue; }
        if (Y >= height) { continue; }

        // bounds check X
        int XStart = HLinePtr->XStart;
        int XEnd   = HLinePtr->XEnd;
        if (XStart >= width) { continue; }
        if (XEnd   <      0) { continue; }
        if (XStart <      0) { XStart = 0; }
        if (XEnd   >= width) { XEnd = width - 1; }

        // Draw each pixel in the current horizontal line in turn,
        // starting with the leftmost one
        for (int X = XStart; X <= XEnd; ++X)
        {
            pCanvas->SetPixel(X, Y, Color);
        }
    }
}

////////////////////////////////////////////////////////////////////////////////
/* Scan converts an edge from (X1,Y1) to (X2,Y2), not including the
   point at (X2,Y2). This avoids overlapping the end of one line with
   the start of the next, and causes the bottom scan line of the
   polygon not to be drawn. If SkipFirst != 0, the point at (X1,Y1)
   isn't drawn. For each scan line, the pixel closest to the scanned
   line without being to the left of the scanned line is chosen. */

/* ScanEdge() is an integer version of following simpler code:

    // Calculate X and Y lengths of the line and the inverse slope
    int DeltaX = X2 - X1;
    int DeltaY = Y2 - Y1;
    if (DeltaY <= 0) { return; } // guard against 0-length and horizontal edges
    float InverseSlope = (float)DeltaX / (float)DeltaY;

    // Store the X coordinate of the pixel closest to but not to the
    // left of the line for each Y coordinate between Y1 and Y2, not
    // including Y2 and also not including Y1 if SkipFirst != 0
    HLine* WorkingEdgePointPtr = *EdgePointPtr;
    for (int Y = Y1 + SkipFirst; Y < Y2; Y++, WorkingEdgePointPtr++)
    {
        // Store the X coordinate in the appropriate edge list
        if (SetXStart == 1)
        {
            WorkingEdgePointPtr->XStart = X1 + (int)(ceil((Y-Y1) * InverseSlope));
        }
        else
        {
            WorkingEdgePointPtr->XEnd = X1 + (int)(ceil((Y-Y1) * InverseSlope));
        }
    }
    *EdgePointPtr = WorkingEdgePointPtr;   // advance caller's ptr
*/
static void ScanEdge(
    int     X1, int Y1,
    int     X2, int Y2,
    int     SetXStart,
    int     SkipFirst,
    HLine** EdgePointPtr)
{
   int ErrorTerm;
   int ErrorTermAdvance, XMajorAdvanceAmt;

   HLine* WorkingEdgePointPtr = *EdgePointPtr; // avoid 2x dereference

   int Height = Y2 - Y1;        // Y length of the edge
   if (Height <= 0) { return; } // guard against 0-length and horizontal edges

   int DeltaX = X2 - X1; // X length of the edge

   // direction in which X moves (Y2 is always > Y1, so Y always counts up)
   int AdvanceAmt = (DeltaX > 0) ? 1 : -1;

   // Figure out if edge is vertical, diagonal, X-major (mostly horizontal),
   // or Y-major (mostly vertical) and handle appropriately
   int Width = ABS(DeltaX);
   if (Width == 0) { // vertical edge: store same X coordinate for every scan line
      // Scan the edge for each scan line in turn
      for (int i = Height - SkipFirst; i-- > 0; WorkingEdgePointPtr++) {
         // Store the X coordinate in the appropriate edge list
         if (SetXStart == 1) { WorkingEdgePointPtr->XStart = X1; }
         else                { WorkingEdgePointPtr->XEnd   = X1; }
      }
   } else if (Width == Height) { // diagonal edge: advance X coordinate 1 pixel each scan line
      // skip the first point if so indicated
      if (SkipFirst) { X1 += AdvanceAmt; } // move 1 pixel to left or right

      // Scan the edge for each scan line in turn
      for (int i = Height - SkipFirst; i-- > 0; WorkingEdgePointPtr++) {
         // Store the X coordinate in the appropriate edge list
         if (SetXStart == 1) { WorkingEdgePointPtr->XStart = X1; }
         else                { WorkingEdgePointPtr->XEnd   = X1; }
         X1 += AdvanceAmt; /* move 1 pixel to the left or right */
      }
   } else if (Height > Width) { // Edge is closer to vertical than horizontal (Y-major)

      // If DeltaX >= 0 then initial error term going left to right else right to left
      ErrorTerm = (DeltaX >= 0) ? 0 : (-Height + 1);

      if (SkipFirst) {   /* skip the first point if so indicated */
         /* Determine whether it's time for the X coord to advance */
         if ((ErrorTerm += Width) > 0) {
            X1 += AdvanceAmt; /* move 1 pixel to the left or right */
            ErrorTerm -= Height; /* advance ErrorTerm to next point */
         }
      }
      // Scan the edge for each scan line in turn
      for (int i = Height - SkipFirst; i-- > 0; WorkingEdgePointPtr++) {
         /* Store the X coordinate in the appropriate edge list */
         if (SetXStart == 1) { WorkingEdgePointPtr->XStart = X1; }
         else                { WorkingEdgePointPtr->XEnd   = X1; }
         /* Determine whether it's time for the X coord to advance */
         if ((ErrorTerm += Width) > 0) {
            X1 += AdvanceAmt; /* move 1 pixel to the left or right */
            ErrorTerm -= Height; /* advance ErrorTerm to correspond */
         }
      }
   } else {
      /* Edge is closer to horizontal than vertical (X-major) */
      /* Minimum distance to advance X each time */
      XMajorAdvanceAmt = (Width / Height) * AdvanceAmt;
      /* Error term advance for deciding when to advance X 1 extra */
      ErrorTermAdvance = Width % Height;

      // If DeltaX >= 0 then initial error term going left to right else right to left
      ErrorTerm = (DeltaX >= 0) ? 0 : (-Height + 1);

      if (SkipFirst) {   /* skip the first point if so indicated */
         X1 += XMajorAdvanceAmt;    /* move X minimum distance */
         /* Determine whether it's time for X to advance one extra */
         if ((ErrorTerm += ErrorTermAdvance) > 0) {
            X1 += AdvanceAmt;       /* move X one more */
            ErrorTerm -= Height; /* advance ErrorTerm to correspond */
         }
      }
      // Scan the edge for each scan line in turn
      for (int i = Height - SkipFirst; i-- > 0; WorkingEdgePointPtr++) {
         // Store the X coordinate in the appropriate edge list
         if (SetXStart == 1) { WorkingEdgePointPtr->XStart = X1; }
         else                { WorkingEdgePointPtr->XEnd   = X1; }

         X1 += XMajorAdvanceAmt;    /* move X minimum distance */
         /* Determine whether it's time for X to advance one extra */
         if ((ErrorTerm += ErrorTermAdvance) > 0) {
            X1 += AdvanceAmt;       /* move X one more */
            ErrorTerm -= Height; /* advance ErrorTerm to correspond */
         }
      }
   }

   *EdgePointPtr = WorkingEdgePointPtr;   /* advance caller's ptr */
}

////////////////////////////////////////////////////////////////////////////////
/* Color-fills a convex polygon. All vertices are offset by (XOffset,
  YOffset). "Convex" means that every horizontal line drawn through
  the polygon at any point would cross exactly two active edges
  (neither horizontal lines nor zero-length edges count as active
  edges; both are acceptable anywhere in the polygon), and that the
  right & left edges never cross. (It's OK for them to touch, though,
  so long as the right edge never crosses over to the left of the
  left edge.) Nonconvex polygons won't be drawn properly. Returns 1
  for success, 0 if memory allocation failed. */

/* Advances the index by one vertex forward through the vertex list,
   wrapping at the end of the list */
#define INDEX_FORWARD(Index) \
   Index = (Index + 1) % Length;

/* Advances the index by one vertex backward through the vertex list,
   wrapping at the start of the list */
#define INDEX_BACKWARD(Index) \
   Index = (Index - 1 + Length) % Length;

/* Advances the index by one vertex either forward or backward through
   the vertex list, wrapping at either end of the list */
#define INDEX_MOVE(Index,Direction)          \
   if (Direction > 0)                        \
      Index = (Index + 1) % Length;          \
   else                                      \
      Index = (Index - 1 + Length) % Length;

int FillConvexPolygon(
    Point*           VertexPtr,
    int32_t          Length,     // number of vertices
    int              Color,
    int XOffset, int YOffset,    // Note: offset only used in fillTest.cpp
    Canvas*          pCanvas)
{
  int i, MinIndexL, MaxIndex, MinIndexR, SkipFirst;
  int MinPoint_Y, MaxPoint_Y, LeftEdgeDir;
  int NextIndex, CurrentIndex, PreviousIndex;
  int DeltaXN, DeltaYN, DeltaXP, DeltaYP;
  HLineList WorkingHLineList;
  HLine *EdgePointPtr;

  /* Scan the list to find the top and bottom of the polygon */
  if (Length == 0) { return 1; } /* reject null polygons */
  MaxPoint_Y = MinPoint_Y = VertexPtr[MinIndexL = MaxIndex = 0].Y;
  for (i = 1; i < Length; i++) {
     if (VertexPtr[i].Y < MinPoint_Y)
     {
        MinPoint_Y = VertexPtr[MinIndexL = i].Y; /* new top */
     }
     else if (VertexPtr[i].Y > MaxPoint_Y)
     {
        MaxPoint_Y = VertexPtr[MaxIndex = i].Y; /* new bottom */
     }
  }
  // polygon is 0-height; avoid infinite loop below
  if (MinPoint_Y == MaxPoint_Y) { return 1; }

  /* Scan in ascending order to find the last top-edge point */
  MinIndexR = MinIndexL;
  while (VertexPtr[MinIndexR].Y == MinPoint_Y) { INDEX_FORWARD(MinIndexR); }
  INDEX_BACKWARD(MinIndexR); /* back up to last top-edge point */

  /* Now scan in descending order to find the first top-edge point */
  while (VertexPtr[MinIndexL].Y == MinPoint_Y) { INDEX_BACKWARD(MinIndexL); }
  INDEX_FORWARD(MinIndexL); /* back up to first top-edge point */

  /* Figure out which direction through the vertex list from the top
     vertex is the left edge and which is the right */
  LeftEdgeDir = -1; /* assume left edge runs down thru vertex list */
  int TopIsFlat = (VertexPtr[MinIndexL].X != VertexPtr[MinIndexR].X) ? 1 : 0;
  if (TopIsFlat == 1) {
     /* If the top is flat, just see which of the ends is leftmost */
     if (VertexPtr[MinIndexL].X > VertexPtr[MinIndexR].X) {
        LeftEdgeDir = 1;  /* left edge runs up through vertex list */
        SWAP(MinIndexL, MinIndexR); // swap so MinIndexL is start of L edge
     }
  } else {
     /* Point to the downward end of the first line of each of the
        two edges down from the top */
     NextIndex = MinIndexR;
     INDEX_FORWARD(NextIndex);
     PreviousIndex = MinIndexL;
     INDEX_BACKWARD(PreviousIndex);
     /* Calculate X and Y lengths from the top vertex to the end of
        the first line down each edge; use those to compare slopes
        and see which line is leftmost */
     DeltaXN = VertexPtr[NextIndex].X - VertexPtr[MinIndexL].X;
     DeltaYN = VertexPtr[NextIndex].Y - VertexPtr[MinIndexL].Y;
     DeltaXP = VertexPtr[PreviousIndex].X - VertexPtr[MinIndexL].X;
     DeltaYP = VertexPtr[PreviousIndex].Y - VertexPtr[MinIndexL].Y;
     if (((long)DeltaXN * DeltaYP - (long)DeltaYN * DeltaXP) < 0L) {
        LeftEdgeDir = 1;  /* left edge runs up through vertex list */
        SWAP(MinIndexL, MinIndexR); // swap so MinIndexL is start of L edge
     }
  }

  /* Set the # of scan lines in the polygon, skipping the bottom edge
     and also skipping the top vertex if the top isn't flat because
     in that case the top vertex has a right edge component, and set
     the top scan line to draw, which is likewise the second line of
     the polygon unless the top is flat */
  if ((WorkingHLineList.Length = MaxPoint_Y - MinPoint_Y - 1 + TopIsFlat) <= 0)
  {
     return(1);  /* there's nothing to draw, so we're done */
  }
  if (WorkingHLineList.Length > MAX_SCREEN_HEIGHT)
  {
      return 0; // will exceed WorkingHLineList.HLinePtr[MAX_SCREEN_HEIGHT]
  }
  WorkingHLineList.YStart = YOffset + MinPoint_Y + 1 - TopIsFlat;

  if (WorkingHLineList.YStart >= pCanvas->Height()) { return 1; } // poly is off screen

  const int YEnd = WorkingHLineList.YStart + WorkingHLineList.Length - 1;
  if (YEnd < 0) { return 1; } // poly is off screen

  /* Commented out in favor of fixed size array to avoid malloc and free
  // Get memory in which to store the line list we generate
  if ((WorkingHLineList.HLinePtr =
        (HLine *)(malloc(sizeof(HLine) *
        WorkingHLineList.Length))) == NULL)
  {
     return(0);  // couldn't get memory for the line list
  }
  */

  /* Scan the left edge and store the boundary points in the list */
  /* Initial pointer for storing scan converted left-edge coords */
  EdgePointPtr = WorkingHLineList.HLinePtr;
  PreviousIndex = CurrentIndex = MinIndexL; // Start from top of left edge
  /* Skip the first point of the first line unless the top is flat;
     if the top isn't flat, the top vertex is exactly on a right
     edge and isn't drawn */
  SkipFirst = TopIsFlat ? 0 : 1;
  /* Scan convert each line in the left edge from top to bottom */
  do {
     INDEX_MOVE(CurrentIndex, LeftEdgeDir);
     ScanEdge(VertexPtr[PreviousIndex].X + XOffset,
              VertexPtr[PreviousIndex].Y,
              VertexPtr[CurrentIndex].X + XOffset,
              VertexPtr[CurrentIndex].Y, 1, SkipFirst, &EdgePointPtr);
     PreviousIndex = CurrentIndex;
     SkipFirst = 0; /* scan convert the first point from now on */
  } while (CurrentIndex != MaxIndex);

  /* Scan the right edge and store the boundary points in the list */
  EdgePointPtr = WorkingHLineList.HLinePtr;
  PreviousIndex = CurrentIndex = MinIndexR;
  SkipFirst = TopIsFlat ? 0 : 1;
  /* Scan convert the right edge, top to bottom. X coordinates are
     adjusted 1 to the left, effectively causing scan conversion of
     the nearest points to the left of but not exactly on the edge */
  do {
     INDEX_MOVE(CurrentIndex, -LeftEdgeDir);
     ScanEdge(VertexPtr[PreviousIndex].X + XOffset - 1,
              VertexPtr[PreviousIndex].Y,
              VertexPtr[CurrentIndex].X + XOffset - 1,
              VertexPtr[CurrentIndex].Y, 0, SkipFirst, &EdgePointPtr);
     PreviousIndex = CurrentIndex;
     SkipFirst = 0; /* scan convert the first point from now on */
  } while (CurrentIndex != MaxIndex);

  /* Draw the line list representing the scan converted polygon */
  DrawHorizontalLineList(&WorkingHLineList, Color, pCanvas);

  /* Release the line list's memory and we're successfully done */
  //free(WorkingHLineList.HLinePtr);

  return(1);
}

/*
////////////////////////////////////////////////////////////////////////////////
// Set up empty object list, with sentinels at both ends to terminate searches
void InitializeObjectList()
{
   ObjectListStart.NextObject     = &ObjectListEnd;
   ObjectListStart.PreviousObject = NULL;
   ObjectListStart.CenterInView.Z = INT_TO_FIXED(-32768);

   ObjectListEnd.NextObject       = NULL;
   ObjectListEnd.PreviousObject   = &ObjectListStart;
   ObjectListEnd.CenterInView.Z   = 0x7FFFFFFFL;

   NumObjects = 0;
}

////////////////////////////////////////////////////////////////////////////////
// Adds an object to the object list, sorted by center Z coord.
void AddObject(Object *ObjectPtr)
{
   Object *ObjectListPtr = ObjectListStart.NextObject;

   // Find the insertion point. Guaranteed to terminate because of the end sentinel
   while (ObjectPtr->CenterInView.Z > ObjectListPtr->CenterInView.Z) {
      ObjectListPtr = ObjectListPtr->NextObject;
   }

   // Link in the new object
   ObjectListPtr->PreviousObject->NextObject = ObjectPtr;
   ObjectPtr->NextObject = ObjectListPtr;
   ObjectPtr->PreviousObject = ObjectListPtr->PreviousObject;
   ObjectListPtr->PreviousObject = ObjectPtr;
   NumObjects++;
}

////////////////////////////////////////////////////////////////////////////////
// Resorts the objects in order of ascending center Z coordinate in view space,
// by moving each object in turn to the correct position in the object list.
void SortObjects()
{
   int i;
   Object *ObjectPtr, *ObjectCmpPtr, *NextObjectPtr;

   // Start checking with the second object
   ObjectCmpPtr = ObjectListStart.NextObject;
   ObjectPtr = ObjectCmpPtr->NextObject;
   for (i=1; i<NumObjects; i++) {
      // See if we need to move backward through the list
      if (ObjectPtr->CenterInView.Z < ObjectCmpPtr->CenterInView.Z) {
         // Remember where to resume sorting with the next object
         NextObjectPtr = ObjectPtr->NextObject;

         // Yes, move backward until we find the proper insertion
         // point. Termination guaranteed because of start sentinel
         do {
            ObjectCmpPtr = ObjectCmpPtr->PreviousObject;
         } while (ObjectPtr->CenterInView.Z <
               ObjectCmpPtr->CenterInView.Z);

         // Now move the object to its new location
         // Unlink the object at the old location
         ObjectPtr->PreviousObject->NextObject =
               ObjectPtr->NextObject;
         ObjectPtr->NextObject->PreviousObject =
               ObjectPtr->PreviousObject;

         // Link in the object at the new location
         ObjectCmpPtr->NextObject->PreviousObject = ObjectPtr;
         ObjectPtr->PreviousObject = ObjectCmpPtr;
         ObjectPtr->NextObject = ObjectCmpPtr->NextObject;
         ObjectCmpPtr->NextObject = ObjectPtr;

         // Advance to the next object to sort
         ObjectCmpPtr = NextObjectPtr->PreviousObject;
         ObjectPtr = NextObjectPtr;
      } else {
         // Advance to the next object to sort
         ObjectCmpPtr = ObjectPtr;
         ObjectPtr = ObjectPtr->NextObject;
      }
   }
}
*/

////////////////////////////////////////////////////////////////////////////////
// Compute screen coordinates based on a physically-based camera model
// http://www.scratchapixel.com/lessons/3d-basic-rendering/3d-viewing-pinhole-camera
// TODO: assumes screen is centered.  Might need to change for stereo cameras.
void computeScreenCoordinates(
    const float widthMm,   //  in: sensor width in mm
    const float heightMm,  //  in: sensor height in mm
    const float nearClipZ, //  in: Usually set to 1
    const float hfov,      //  in: horizontal field of view in degrees
    float       &top,      // out: screen coordinates on nearClipPlane
    float       &bottom,
    float       &left,
    float       &right)
{
    /* Looking down on sensor from above:

                     -Z axis
                        ^
                        |
            +-----------------------+ projection plane
          ^  \                     /
          |   \      widthMm      /
          |    \<--------------->/
          |     +-------+-------+  sensor plane
          |      \      |      /      ^
          |       \     |     /       |
       nearClipZ   \    |    /        |
          |         \   |<->/        focalLength
          |          \  |  / hfov/2   |
          |           \ | /           |
          v            \|/            v
        <---+-----------+-----------+-----> X axis
           left         0         right

             Camera viewpoint at (0, 0, 0)

       tan(angleInRadians      ) =    opposite   / adjacent
       tan(radPerDeg * hfov / 2) = (widthMm / 2) / focalLength
    */

    //     hfov =  2.0f * atan((widthMm  / 2) / focalLength)
    //     hfov / 2.0f  = atan((widthMm  / 2) / focalLength)
    // tan(hfov / 2.0)  =      (widthMm  / 2) / focalLength
    Fixedpoint hfovDiv2 = DOUBLE_TO_FIXED(hfov / 2.0);
    const double focalLength = (widthMm / 2.0) / FIXED_TO_DOUBLE(tanFixed(hfovDiv2));

    // Due to similar triangles: right / nearClipZ = (widthMm / 2) / focalLength
    right  = (float)(((widthMm  / 2.0) / focalLength) * nearClipZ);
    top    = (float)(((heightMm / 2.0) / focalLength) * nearClipZ);
    bottom = -top;
    left   = -right;
}
