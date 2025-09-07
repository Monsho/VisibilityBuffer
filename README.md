# VisibilityBuffer

## Building
1. Pull repository and update all submodules.
2. Open SampleLib/SampleLib12.sln in Visual Studio 2022, and restore NuGet packages.
3. Open App/VisibilityBuffer.sln in Visual Studio 2022, and build solution.

## Execution
+ The following command line arguments are available.
  + -homedir <dir>
    + Home directories can be specified as absolute or relative paths.
    + Set the parent directory of the "resource" directory.
    + When running Visual Studio debugging, ".." to be specified.
  + -mesh <0/1>
    + Specify mesh type.
    + 0: Many suzanne
    + 1: Sponza
    + 2: Intel Sponza + Curtain (option / See Texture Streaming sample)
  + -hdr
    + HDR display mode.
    + if display do NOT support HDR, the option is ignored.
  + -res <WIDTHxHEIGHT>
    + Specify window resolution.
    + exp.) -res 1280x720

## Texture Streaming sample
1. Download IntelMesh.zip and unzip in resources/mesh directory.
   + https://www.dropbox.com/s/mhso4568i88iouf/IntelMesh.zip?dl=0
2. "-mesh 2" in startup argument.

## How to use Lumberyard Bistro
1. Execute App/resources/mesh/MergeBistro.py
2. Extract Bistro.zip in App/resources/mesh/Bistro
3. "-mesh 3" in startup argument.
