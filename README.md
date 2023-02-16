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
