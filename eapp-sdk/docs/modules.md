# Modules

The host loads one image of at most 1 MiB, code, data and bss together. A module is more code: a `<name>.emod` file of the package that the app reads into a heap block while it runs. Several modules can be loaded at once, each at whatever address its block gets, and each can be freed again. Modules work on any host that runs the image, because the runtime loads them itself.

## Building

```make
MODULES := physics level
physics_SOURCES := physics.c
level_SOURCES := level/rooms.c level/doors.c
```

`eapp.mk` compiles each module's sources with the app's flags, links the module at address 0 against the finished image and packs `physics.emod` and `level.emod` into the package. A module is tied to the image it was linked against: every change to the image relinks and repacks its modules.

## Calling a module

A header that both sides include declares the module's table, a struct of the app's own type. The module defines it with `EAPP_MODULE`; `eapp_module_load` returns it.

```c
/* physics.h */
struct physics {
    void (*step)(struct world *world, int ms);
    int (*collides)(const struct body *a, const struct body *b);
};

/* physics.c, the module */
static void step(struct world *world, int ms) { /* ... */ }
static int collides(const struct body *a, const struct body *b) { /* ... */ }
EAPP_MODULE(struct physics) = {step, collides};

/* main.c, the image */
const struct physics *physics = eapp_module_load("physics.emod");
if (physics)
    physics->step(&world, 16);
eapp_module_free(physics);
```

`eapp_module_load` returns NULL and logs the reason when the file is missing, is not a module, was built for another image or does not fit the heap. `eapp_module_free` frees the block; every pointer into the module, its functions, strings and data, is invalid afterwards.

## What a module can use

| | |
|---|---|
| The image's functions and globals | Called and used directly, SDK functions included. The build keeps whatever a module names in the image, even when the image never calls it |
| Its own data and bss | Start fresh at every load; two loads of one file have two copies |
| The image's `static` functions and data | Not reachable |
| Another module | Only through a table the image passes on |
| Constructors, `.init_array`, C++ exceptions | Not run; a module with constructors fails to link |

`eapp-pack` refuses a module that defines a global the image also defines, since each would use its own. A module links its own copy of the libgcc helpers it uses, such as division, so it never depends on the helpers the image happens to hold; Thumb code calls some of them with short calls, which cannot reach the image from the heap.

## Memory

A loaded module takes its text, data and bss in one block of the heap, which it shares with the app's own blocks: the modules loaded at once and the app's data together must fit the heap of `HEAP_KIB`. Code that is not needed at the same time can live in modules that are loaded and freed in turn. `eapp-pack` prints each module's block size and relocation count. Loading reads the relocations in pieces of 256 bytes, so it needs no second block.

The format is in [Image](image.md).
