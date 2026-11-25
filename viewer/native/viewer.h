#ifndef ACPC_VIEWER_H
#define ACPC_VIEWER_H

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#define AV_PATH 2048
#define AV_NAME 96
#define AV_LODS 32
#define AV_MAX_CORNERS 6000000u
#define AV_FILE_LIMIT (512u * 1024u * 1024u)

#define AV_UNUSED(x) ((void)(x))

typedef struct { float x, y; } Vec2;
typedef struct { float x, y, z; } Vec3;

typedef struct {
    Vec3 position;
    Vec3 normal;
    Vec2 uv;
    Vec2 light_uv;
} AvVertex;

typedef struct { uint32_t vertex[3], material; } AvTriangle;

typedef struct {
    AvVertex *vertices;
    AvTriangle *triangles;
    size_t vertex_count, triangle_count;
    size_t vertex_capacity, triangle_capacity;
    Vec3 center;
    float radius;
    int has_light_uv;
} AvMesh;

typedef struct {
    int width, height;
    uint32_t *pixels;
} AvImage;

typedef struct {
    char path[AV_PATH];
    AvImage image;
} AvTexture;

typedef struct {
    char name[AV_NAME];
    uint32_t color;
    int diffuse, light;
} AvMaterial;

typedef enum { AV_NONE, AV_BMF, AV_MSH, AV_OBJ, AV_DEMO } AvKind;

typedef struct {
    AvMesh lods[AV_LODS];
    uint32_t lod_count;
    AvMaterial *materials;
    size_t material_count, material_capacity;
    AvTexture *textures;
    size_t texture_count, texture_capacity, texture_bytes;
    unsigned char *bmf;
    size_t bmf_size;
    uint32_t frame_count, frame;
    int missing_textures;
    AvKind kind;
    char path[AV_PATH];
    char mtl_ref[AV_PATH];
    char error[256];
} AvModel;

typedef enum {
    AV_TEXTURED, AV_SOLID, AV_WIREFRAME, AV_UV0, AV_NORMALS, AV_LIGHT, AV_UV1
} AvMode;

typedef struct { float yaw, pitch, zoom, pan_x, pan_y; } AvView;

typedef struct {
    int width, height;
    uint32_t *pixels;
    float *depth;
} AvSurface;

static inline Vec3 av_add(Vec3 a, Vec3 b) {
    return (Vec3){a.x + b.x, a.y + b.y, a.z + b.z};
}
static inline Vec3 av_sub(Vec3 a, Vec3 b) {
    return (Vec3){a.x - b.x, a.y - b.y, a.z - b.z};
}
static inline Vec3 av_scale(Vec3 a, float b) {
    return (Vec3){a.x * b, a.y * b, a.z * b};
}
static inline float av_dot(Vec3 a, Vec3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
static inline Vec3 av_cross(Vec3 a, Vec3 b) {
    return (Vec3){a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
static inline Vec3 av_normal(Vec3 a) {
    float length2 = av_dot(a, a);
    return length2 > 1e-20f && length2 < 1e30f ? av_scale(a, 1.0f / sqrtf(length2)) : (Vec3){0, 1, 0};
}
static inline float av_clamp(float v, float low, float high) {
    return v < low ? low : v > high ? high : v;
}
static inline float av_min(float a, float b) { return a < b ? a : b; }
static inline float av_max(float a, float b) { return a > b ? a : b; }
static inline uint32_t av_u32(const void *bytes) {
    const unsigned char *p = bytes;
    return p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static inline uint32_t av_u16(const void *bytes) {
    const unsigned char *p = bytes;
    return p[0] | ((uint32_t)p[1] << 8);
}
static inline float av_f32(const void *bytes) {
    uint32_t bits = av_u32(bytes);
    float value;
    memcpy(&value, &bits, sizeof(value));
    return value;
}
static inline int av_isfinite(float v) { return v >= -1e20f && v <= 1e20f; }

FILE *av_fopen(const char *path, const char *mode);
unsigned char *av_read_file(const char *path, size_t *size);
int av_platform_image(const char *path, AvImage *image);
int av_dds(const unsigned char *data, size_t size, AvImage *image);
int av_bmp(const unsigned char *data, size_t size, AvImage *image);
int av_save_bmp(const char *path, const AvSurface *surface);

int av_equals(const char *a, const char *b);
const char *av_filename(const char *path);
const char *av_extension(const char *path);
void av_copy(char *out, size_t capacity, const char *text);
int av_reserve(void **pointer, size_t *capacity, size_t needed, size_t element_size);
void av_mesh_bounds(AvMesh *mesh);
void av_model_free(AvModel *model);
int av_model_open(AvModel *model, const char *path, int obj_z_up);
int av_model_set_frame(AvModel *model, uint32_t frame);
int av_model_demo(AvModel *model);
int av_model_set_diffuse(AvModel *model, unsigned material, const char *path);
int av_write_obj(const AvModel *model, const char *path, unsigned lod);
int av_surface_size(AvSurface *surface, int width, int height);
void av_surface_free(AvSurface *surface);
void av_view_reset(AvView *view);
void av_render(AvSurface *surface, const AvModel *model, unsigned lod, const AvView *view, AvMode mode);

#endif
