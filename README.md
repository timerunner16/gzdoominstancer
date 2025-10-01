# GZDoomInstancer
*The GZDoom Mod Manager of Your Dreams.*

## Purpose
Many times I've had multiple collections of mods I want to play with,
each with separate settings and saves and whatnot, but the way most
existing launchers worked prevented me from effectively separating
all of this data. GZDoomInstancer is created to solve this problem
of clashing data.

## Description
The launcher separates everything into 'instances' - essentially,
a directory containing all the save, config, and mod data, completely
independent from all other instances. This way, when loading one
instance, you'll be able to modify your settings or load your saves
without worrying about messing up your other instances.

For the sake of limiting the amount of duplicated data, IWADs and
PWADs are stored in respective central pools, while instances just
reference them by name. This way, you can have a hundred instances
all with the same mod, and it won't take more than a few kilobytes
above the size of the mod itself.

## Usage
GZDoomInstancer is split into three views.

### Instance Manager/Launcher
Select an instance, create new instances, launch the instance, begin
editing the instance, etc. You may need to specify the path where
GZDoom is stored if it's outside of the default.

### Instance Editor
Select the IWADs and PWADs associated with an existing instance, or
add new ones.

### idGames Browser
Search through the idGames archive and download PWADs without ever
leaving the launcher, through this browser created using the idGames
API provided by Doomworld.

## Building
### Linux
Install build tools Git, CMake, and a CXX compiler (Ubuntu command):
```bash
sudo apt update && \
sudo apt install git build-essential cmake
```

Install libraries CURL, LibZip, SDL2, and Glew (Ubuntu command):

```bash
sudo apt update && \
sudo apt install libcurl4-openssl-dev libzip-dev libsdl2-dev libglew-dev
```

Clone the repository:

```bash
git clone https://github.com/timerunner16/gzdoominstancer
```

Create a build directory:

```bash
mkdir gzdoominstancer/build && cd gzdoominstancer/build
```

Generate build files and build:

```bash
cmake .. && \
cmake --build .
```


### Windows
Currently, GZDoomInstancer only supports Linux devices, but support
is coming to Windows soon.
