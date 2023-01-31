# Grayscale image sensor simulator

Vanilla (OS and HW agnostic) C++ code to generate simulated images for a
grayscale image sensor.

## Build/Test/Run
1. setup source and build dirs: `cmake -S . -B build`
2. build: `cmake --build build --config Release`
3. Run tests: `ctest -V --test-dir build`
4. Run: `.\build\Release\CubeTest.exe`

## Design criteria

1. No external libraries are used to minimize dependencies.
1. Camera and scene objects have position and orientation velocity vectors
to simulate dynamic scenes.
1. Objects are composed of planar polygons.  Each polygon emits a fixed photons
per second to speed/simplify rendering.
1. This code runs on CPU to generate near real-time input to a GPU video
processing pipeline.
1. The ideal image (and ideal locations/orientations) can be generated for
comparison with GPU video processing pipeline output.
1. Floats are not used in favor of faster 32-bit fixed point values (1 sign bit, 15 integer
bits, 16 fractional bits). This increases possibility of on-demand on-device
simulation.  This also enables use on devices without FPU's.
1. Time units are integer microseconds (a common sensor exposure unit).
1. x,y,z position units are in fixed point centimeters:  
--> abs(min) = 2^(-16) = 1.52e-5 cm = 1.52e-4 mm  
--> abs(max) = 2^15 = 32768 cm = 327.68 m
1. dx,dy,dz velocity units are in centimeters per microsecond  
--> min = 2^(-16) cm/usec = 0.341 mph (also min speed increment)
1. rotation velocity units are in tenths of a degree per microsecond. Example:
a helicopter blade at 500 rpm = 0.03 (deg/10) / usec  
--> min = 2^(-16) (deg/10)/usec = 0.254 rpm (and min rpm increment)

## Simulation flow diagram (order is important)
```
+--------------------------------------------------+
|             Render 3D to 2D                      |
|                                                  |
| For each line group: (for rolling shutter)       |
|     Init lines to zero photons                   |
|     Compute camera frustum of line group         |<-- objects,
|     For each sub-exposure: (for motion blur)     |    absolute time,
|         Update camera/object location/orientation|    exposure time,
|         Render to add photons to line group      |    lines per sub-exposure,
|         Increment time by subexposure time       |    read-out time
|     reset time to start of line group exposure   |
|     increment time by line group read-out time   |
+--------------------------------------------------+
                     |
                     v
+--------------------------------------------------+
|              Lens distort                        |    enable,
|                                                  |<-- LUT,
|      X = x - xc; Y = y - yc; R = hypot(X, Y)     |    dist. center (xc, yc)
| out(x,y) = in(X * LUT[R] + xc, Y * LUT[R] + yc)  |
+--------------------------------------------------+
                     |
                     v
+--------------------------------------------------+
|                 Lens blur                        |
|                                                  |<-- enable, point-spread
|         out = convolve(in, PSF)                  |    function (PSF)
+--------------------------------------------------+
                     |
                     V
+--------------------------------------------------+
|  Relative Illumination (aka lens vignetting)     |
|                                                  |<-- enable,
|               out = in * LUT[radius]             |    LUT
+--------------------------------------------------+
                     |
                     V
+--------------------------------------------------+
|               Add Dark frame                     |
|                                                  |<-- enable,
|            out = in + darkFrame                  |    darkFrame
+--------------------------------------------------+
                     |
                     v
+--------------------------------------------------+
|              Add Poisson noise                   |
|                                                  |<-- LUT
|                out = LUT[in][rand()]             |
+--------------------------------------------------+
                     |
                     V
+--------------------------------------------------+
|       Pixel value compression to 8 bits          |
|                                                  |<-- enable,
|                out = LUT[in]                     |    LUT
+--------------------------------------------------+
                     |
                     V
                output image
```

## TODO
Roughly in order they should be done:
- [ ] Add quaternions to specify camera and object orientation (currently specified
as angles around x, y and z axes)
- [ ] Add motion blur rendering (sum of sub-exposures where sub-exposure time
is a function of relative velocity between camera and objects)
- [ ] Add line group rendering to simulate rolling-shutter
- [ ] Support for uint8 and uint16 output (currently only uint32)
- [ ] Add back-to-front rendering of objects to improve occlusion accuracy
- [ ] Add z-buffer to replace back-to-front rendering (but z-buf might be too slow)
- [ ] Add photon per second attribute for each vertex (currently polygon faces
have same photons per second)
- [ ] Add (U,V) coordinates to vertices for texture mapping:
  - Textures would be in 8-bit nonlinear compressed space.
  - Linear values in [0, 10837] that are in units of photons/second.
  - min =     1 photons/s or 0.011 photons per 11 msec exposure
  - max = 10837 photons/s or 120 photons per 11 msec exposure
  - Bilinear interpolation of texture helps hide banding due to 8-bit compression.

