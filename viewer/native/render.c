#include "viewer.h"

typedef struct {
    float x, y, z;
    Vec3 normal;
    Vec2 uv;
} CameraVertex;

typedef struct {
    float x, y, inv_z;
    float u_over_z, v_over_z;
    Vec3 n_over_z;
    float light_over_z;
} ScreenVertex;

typedef struct {
    AvSurface *target;
    const AvImage *texture;
    AvMode mode;
    uint32_t color;
} Paint;

int av_surface_size(AvSurface *surface, int width, int height) {
    if (width == surface->width && height == surface->height && surface->pixels) return 1;
    if (width <= 0 || height <= 0 || width > 4096 || height > 4096) return 0;
    size_t count = (size_t)width * (size_t)height;
    uint32_t *pixels = malloc(count * sizeof(*pixels));
    float *depth = malloc(count * sizeof(*depth));
    if (!pixels || !depth) { free(pixels); free(depth); return 0; }
    av_surface_free(surface);
    *surface = (AvSurface){width, height, pixels, depth};
    return 1;
}

void av_surface_free(AvSurface *surface) {
    free(surface->pixels);
    free(surface->depth);
    *surface = (AvSurface){0};
}

void av_view_reset(AvView *view) {
    *view = (AvView){2.58f, -0.28f, 1.0f, 0, 0};
}

static void clear_surface(AvSurface *surface) {
    int width = surface->width, height = surface->height;
    const int band = 48;
    for (int y = 0; y < height; ++y) {
        unsigned r = (unsigned)(16 + 8 * y / height);
        unsigned g = (unsigned)(24 + 9 * y / height);
        unsigned b = (unsigned)(36 + 11 * y / height);
        uint32_t row = 0xff000000U | (r << 16) | (g << 8) | b;
        for (int x = 0; x < width; ++x) {
            uint32_t dot = x % band == width / 2 % band && y % band == height / 2 % band ? 0x000a0a0aU : 0;
            surface->pixels[(size_t)y * (size_t)width + (size_t)x] = row + dot;
        }
    }
    memset(surface->depth, 0, (size_t)width * (size_t)height * sizeof(float));
}

static CameraVertex between(CameraVertex a, CameraVertex b, float t) {
    CameraVertex out;
    out.x = a.x + (b.x-a.x)*t;
    out.y = a.y + (b.y-a.y)*t;
    out.z = a.z + (b.z-a.z)*t;
    out.normal = av_add(a.normal, av_scale(av_sub(b.normal, a.normal), t));
    out.uv = (Vec2){a.uv.x + (b.uv.x-a.uv.x)*t, a.uv.y + (b.uv.y-a.uv.y)*t};
    return out;
}

static unsigned clip_near(CameraVertex out[4], const CameraVertex in[3], float near) {
    unsigned n = 0;
    for (unsigned i = 0; i < 3; ++i) {
        CameraVertex a = in[i], b = in[(i+1)%3];
        int a_in = a.z >= near, b_in = b.z >= near;
        if (a_in) out[n++] = a;
        if (a_in != b_in) {
            CameraVertex point = between(a, b, (near-a.z)/(b.z-a.z));
            point.z = near;
            out[n++] = point;
        }
    }
    return n;
}

static ScreenVertex project(CameraVertex p, float focal, float cx, float cy) {
    float q = 1 / p.z;
    float light = 0.49f + 0.51f * fabsf(av_dot(p.normal, (Vec3){0.29f, 0.77f, -0.57f}));
    return (ScreenVertex){cx + p.x*focal*q, cy - p.y*focal*q, q, p.uv.x*q, p.uv.y*q,
        av_scale(p.normal, q), light*q};
}

static float edge(ScreenVertex a, ScreenVertex b, float x, float y) {
    return (b.x-a.x)*(y-a.y) - (b.y-a.y)*(x-a.x);
}

static uint32_t illuminated(uint32_t color, float factor) {
    unsigned r = (unsigned)((float)((color >> 16) & 255U) * factor);
    unsigned g = (unsigned)((float)((color >> 8) & 255U) * factor);
    unsigned b = (unsigned)((float)(color & 255U) * factor);
    return 0xff000000U | (r << 16) | (g << 8) | b;
}

static uint32_t normal_color(Vec3 n) {
    unsigned r = (unsigned)((av_clamp(n.x, -1, 1)*0.5f+0.5f)*255);
    unsigned g = (unsigned)((av_clamp(n.y, -1, 1)*0.5f+0.5f)*255);
    unsigned b = (unsigned)((av_clamp(n.z, -1, 1)*0.5f+0.5f)*255);
    return 0xff000000U | (r << 16) | (g << 8) | b;
}

static float repeat(float v) {
    /* UVs outside 0–1 are intentional and repeat on the preview. */
    if (!(v > -1e7f && v < 1e7f)) return 0;
    int integer = (int)v;
    float fraction = v - (float)integer;
    return fraction < 0 ? fraction + 1 : fraction;
}

static uint32_t shade(const Paint *paint, float u, float v, float light, Vec3 normal) {
    if (paint->mode == AV_NORMALS) return normal_color(normal);
    uint32_t base = paint->color;
    if (paint->mode == AV_UV0 || paint->mode == AV_UV1) {
        int square = (int)(repeat(u)*16) + (int)(repeat(v)*16);
        base = square & 1 ? 0xff75b7d7U : 0xff273c53U;
    } else if (paint->texture) {
        int width = paint->texture->width, height = paint->texture->height;
        int x = (int)(repeat(u) * (float)width), y = (int)(repeat(v) * (float)height);
        if (x >= width) x = width - 1;
        if (y >= height) y = height - 1;
        base = paint->texture->pixels[(size_t)y * (size_t)width + (size_t)x];
    }
    return illuminated(base, av_clamp(light, 0, 1));
}

static void fill_triangle(Paint *paint, ScreenVertex a, ScreenVertex b, ScreenVertex c) {
    AvSurface *surface = paint->target;
    float area = edge(a, b, c.x, c.y);
    if (fabsf(area) < 0.001f) return;
    const float max_x = (float)(surface->width-1), max_y = (float)(surface->height-1);
    float left = av_min(a.x, av_min(b.x, c.x)), right = av_max(a.x, av_max(b.x, c.x));
    float top = av_min(a.y, av_min(b.y, c.y)), bottom = av_max(a.y, av_max(b.y, c.y));
    if (left > max_x || right < 0 || top > max_y || bottom < 0) return;
    int x0 = (int)av_clamp(left, 0, max_x), x1 = (int)av_clamp(right + 1, 0, max_x);
    int y0 = (int)av_clamp(top, 0, max_y), y1 = (int)av_clamp(bottom + 1, 0, max_y);
    float inv_area = 1 / area;
    float x_step_a = (b.y-c.y)*inv_area, x_step_b = (c.y-a.y)*inv_area;
    for (int y = y0; y <= y1; ++y) {
        float wa = edge(b, c, (float)x0+0.5f, (float)y+0.5f)*inv_area;
        float wb = edge(c, a, (float)x0+0.5f, (float)y+0.5f)*inv_area;
        size_t offset = (size_t)y * (size_t)surface->width + (size_t)x0;
        for (int x = x0; x <= x1; ++x, ++offset, wa += x_step_a, wb += x_step_b) {
            float wc = 1-wa-wb;
            if (wa < -1e-5f || wb < -1e-5f || wc < -1e-5f) continue;
            float z = wa*a.inv_z + wb*b.inv_z + wc*c.inv_z;
            if (z <= surface->depth[offset]) continue;
            surface->depth[offset] = z;
            float inv = 1/z;
            float u = (wa*a.u_over_z + wb*b.u_over_z + wc*c.u_over_z)*inv;
            float v = (wa*a.v_over_z + wb*b.v_over_z + wc*c.v_over_z)*inv;
            float light = (wa*a.light_over_z + wb*b.light_over_z + wc*c.light_over_z)*inv;
            Vec3 normal = av_scale(av_add(av_scale(a.n_over_z, wa),
                av_add(av_scale(b.n_over_z, wb), av_scale(c.n_over_z, wc))), inv);
            surface->pixels[offset] = shade(paint, u, v, light, normal);
        }
    }
}

static int clip_line(float p, float q, float *t0, float *t1) {
    if (p == 0) return q >= 0;
    float t = q / p;
    if (p < 0) {
        if (t > *t1) return 0;
        if (t > *t0) *t0 = t;
    } else {
        if (t < *t0) return 0;
        if (t < *t1) *t1 = t;
    }
    return 1;
}

static void wire_line(AvSurface *surface, ScreenVertex a, ScreenVertex b) {
    float dx = b.x-a.x, dy = b.y-a.y;
    float t0 = 0, t1 = 1;
    if (!clip_line(-dx, a.x, &t0, &t1) || !clip_line(dx, (float)(surface->width-1)-a.x, &t0, &t1)
        || !clip_line(-dy, a.y, &t0, &t1) || !clip_line(dy, (float)(surface->height-1)-a.y, &t0, &t1)) return;
    int steps = (int)(av_max(fabsf(dx), fabsf(dy))*(t1-t0)) + 1;
    for (int i = 0; i <= steps; ++i) {
        float t = t0 + (t1-t0)*(float)i/(float)steps;
        int x = (int)av_clamp(a.x + dx*t, 0, (float)(surface->width-1));
        int y = (int)av_clamp(a.y + dy*t, 0, (float)(surface->height-1));
        surface->pixels[(size_t)y * (size_t)surface->width + (size_t)x] = 0xff83c9dcU;
    }
}

void av_render(AvSurface *surface, const AvModel *model, unsigned lod, const AvView *view, AvMode mode) {
    if (!surface->pixels) return;
    clear_surface(surface);
    if (lod >= model->lod_count || !model->lods[lod].triangle_count) return;
    const AvMesh *mesh = &model->lods[lod];
    float cw = cosf(view->yaw), sw = sinf(view->yaw), cp = cosf(view->pitch), sp = sinf(view->pitch);
    float distance = mesh->radius * 2.8f * view->zoom;
    float near = av_max(mesh->radius * 0.003f, 1e-5f);
    float width = (float)surface->width, height = (float)surface->height;
    float focal = av_min((float)width, (float)height) * 1.08f;
    float cx = width * 0.5f + view->pan_x*width, cy = height * 0.5f + view->pan_y*height;
    Paint paint = {surface, NULL, mode, 0xffbac6d5U};
    int uv1 = mode == AV_LIGHT || mode == AV_UV1;
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        const AvTriangle *triangle = &mesh->triangles[i];
        CameraVertex camera[3];
        for (unsigned c = 0; c < 3; ++c) {
            const AvVertex *source = &mesh->vertices[triangle->vertex[c]];
            Vec3 p = av_sub(source->position, mesh->center), n = source->normal;
            float yaw_z = p.z*cw - p.x*sw, normal_z = n.z*cw - n.x*sw;
            camera[c].x = p.x*cw + p.z*sw;
            camera[c].y = p.y*cp - yaw_z*sp;
            camera[c].z = distance + p.y*sp + yaw_z*cp;
            camera[c].normal = (Vec3){n.x*cw+n.z*sw, n.y*cp-normal_z*sp, n.y*sp+normal_z*cp};
            camera[c].uv = uv1 ? source->light_uv : source->uv;
        }
        CameraVertex clamped[4];
        unsigned count = clip_near(clamped, camera, near);
        if (count < 3) continue;
        ScreenVertex screen[4];
        for (unsigned j = 0; j < count; ++j) screen[j] = project(clamped[j], focal, cx, cy);
        if (mode == AV_WIREFRAME) {
            for (unsigned j = 0; j < count; ++j) wire_line(surface, screen[j], screen[(j+1)%count]);
            continue;
        }
        const AvMaterial *material = &model->materials[triangle->material];
        paint.color = mode == AV_SOLID ? 0xffb7c2d1U : material->color;
        int texture = mode == AV_TEXTURED ? material->diffuse : mode == AV_LIGHT ? material->light : -1;
        paint.texture = texture >= 0 ? &model->textures[texture].image : NULL;
        for (unsigned j = 1; j+1 < count; ++j) fill_triangle(&paint, screen[0], screen[j], screen[j+1]);
    }
}
