#include "model_motion.h"
#include <cassert>
#include <cstring>
#include <iostream>
#include <limits>
using namespace gsr;
int main() {
    motion::Models history;
    auto scene = std::make_shared<SceneModels>();
    scene->level = 1;
    auto mesh = std::make_shared<SceneModelMesh>();
    mesh->identity = 1;
    mesh->topology = 123;
    mesh->nativeId = 2;
    mesh->triangles.resize(1);
    mesh->triangles[0].position[1][0] = 10;
    mesh->triangles[0].position[2][1] = 10;
    scene->meshes.push_back(mesh);
    SceneModelInstance i;
    i.object = 100;
    i.generation = 1;
    i.transform[0] = i.transform[5] = i.transform[10] = 1;
    scene->instances.push_back(i);
    assert(history.prepare(*scene, false).vertices.empty());
    history.commit(scene);
    assert(history.prepare(*scene, false).vertices.empty());
    auto moved = std::make_shared<SceneModels>(*scene);
    moved->instances[0].transform[3] = 7;
    auto f = history.prepare(*moved, false);
    assert(f.matched == 1 && f.vertices.size() == 3);
    assert(f.vertices[0].current[0] == 7 && f.vertices[0].previous[0] == 0);
    assert(history.prepare(*moved, true).vertices.empty());
    auto pose = std::make_shared<SceneModelMesh>(*mesh);
    pose->identity = 2;
    pose->triangles[0].position[0][1] = 3;
    moved->meshes[0] = pose;
    f = history.prepare(*moved, false);
    assert(f.vertices[0].current[1] == 3 && f.vertices[0].previous[1] == 0);
    pose->topology = 124;
    assert(history.prepare(*moved, false).vertices.empty());
    pose->topology = 123;
    moved->instances[0].generation = 2;
    assert(history.prepare(*moved, false).vertices.empty());
    moved->instances[0].generation = 1;
    moved->level = 2;
    assert(history.prepare(*moved, false).vertices.empty());
    moved->level = 1;
    moved->instances.push_back(moved->instances[0]);
    assert(history.prepare(*moved, false).vertices.empty());
    moved->instances.pop_back();
    moved->instances[0].opacity = .5f;
    assert(history.prepare(*moved, false).vertices.empty());
    moved->instances[0].opacity = 1;
    moved->instances[0].opacity = std::numeric_limits<float>::quiet_NaN();
    assert(history.prepare(*moved, false).vertices.empty());
    moved->instances[0].opacity = std::numeric_limits<float>::infinity();
    assert(history.prepare(*moved, false).vertices.empty());
    moved->instances[0].opacity = 1;
    scene->instances[0].opacity = std::numeric_limits<float>::quiet_NaN();
    assert(history.prepare(*moved, false).vertices.empty());
    scene->instances[0].opacity = 1;
    auto texture = std::make_shared<SceneModelTexture>();
    texture->flags = 8;
    texture->pixels.resize(128 * 128, 0x80ffffff);
    moved->textures.push_back(texture);
    pose->triangles[0].material = 0;
    f = history.prepare(*moved, false);
    assert(f.vertices.size() == 3 && f.vertices[0].alphaTexture == 0);
    // Hundreds of unused resident materials must not be copied into moving
    // actor guides; source indices are remapped only after valid poses survive.
    moved->textures.resize(200, texture);
    pose->triangles[0].material = 199;
    f = history.prepare(*moved, false);
    assert(f.textures.size() == 1 && f.vertices[0].alphaTexture == 0);
    assert(f.textures[0].pixels.size() == texture->pixels.size() &&
           f.textures[0].pixels.data() == texture->pixels.data());
    // The explicit-previous form (used on the builder thread) and a reused frame give the same result.
    {
        motion::Models::Frame reused;
        motion::Models::Scratch scratch;
        reused.vertices.resize(50); // stale content must not survive
        motion::Models::prepareFrom(history.previous().get(), *moved, false, reused, scratch);
        assert(reused.matched == f.matched && reused.rejected == f.rejected &&
               reused.vertices.size() == f.vertices.size() && reused.textures.size() == f.textures.size());
        for (size_t n = 0; n < f.vertices.size(); ++n)
            assert(std::memcmp(&reused.vertices[n], &f.vertices[n], sizeof(f.vertices[n])) == 0);
        motion::Models::prepareFrom(nullptr, *moved, false, reused, scratch);
        assert(reused.vertices.empty() && reused.textures.empty() && reused.matched == 0);
    }
    texture->flags = 0;
    f = history.prepare(*moved, false);
    assert(f.vertices.size() == 3 && f.textures.empty());
    texture->flags = 0x80000;
    assert(history.prepare(*moved, false).vertices.empty());
    history.reset();
    assert(history.prepare(*moved, false).vertices.empty());
    std::cout << "PASS native model translation/deformation correspondence, lifetime, level, topology, ambiguity, "
                 "alpha and reset\n";
}
