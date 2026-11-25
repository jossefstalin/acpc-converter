#include "viewer.h"

#define AV_DEFAULT_COLOR 0xffb9caddU


typedef struct {
    const unsigned char *data;
    size_t size, offset;
    int bad;
} Reader;

static const unsigned char *take(Reader *r, size_t size) {
    if (r->bad || size > r->size - r->offset) {
        r->bad = 1;
        return NULL;
    }
    const unsigned char *p = r->data + r->offset;
    r->offset += size;
    return p;
}

static uint32_t next_u32(Reader *r) {
    const unsigned char *p = take(r, 4);
    return p ? av_u32(p) : 0;
}

static char lower_ascii(char c) { return c >= 'A' && c <= 'Z' ? (char)(c + 'a' - 'A') : c; }

int av_equals(const char *a, const char *b) {
    while (*a && lower_ascii(*a) == lower_ascii(*b)) { ++a; ++b; }
    return lower_ascii(*a) == lower_ascii(*b);
}

const char *av_filename(const char *path) {
    const char *name = path;
    for (; *path; ++path) if (*path == '/' || *path == '\\') name = path + 1;
    return name;
}

const char *av_extension(const char *path) {
    const char *extension = strrchr(av_filename(path), '.');
    return extension ? extension : "";
}

void av_copy(char *out, size_t capacity, const char *text) {
    if (capacity == 0) return;
    size_t count = strlen(text);
    if (count >= capacity) count = capacity - 1;
    memmove(out, text, count);
    out[count] = '\0';
}

int av_reserve(void **pointer, size_t *capacity, size_t needed, size_t element_size) {
    if (needed <= *capacity) return 1;
    size_t total = *capacity ? *capacity : 32;
    while (total < needed) {
        if (total > SIZE_MAX / 2) return 0;
        total *= 2;
    }
    if (total > SIZE_MAX / element_size) return 0;
    void *grown = realloc(*pointer, total * element_size);
    if (!grown) return 0;
    *pointer = grown;
    *capacity = total;
    return 1;
}

unsigned char *av_read_file(const char *path, size_t *size) {
    *size = 0;
    FILE *stream = av_fopen(path, "rb");
    if (!stream) return NULL;
    if (fseek(stream, 0, SEEK_END) != 0) { fclose(stream); return NULL; }
    long length = ftell(stream);
    if (length <= 0 || length > (long)AV_FILE_LIMIT || fseek(stream, 0, SEEK_SET) != 0) {
        fclose(stream);
        return NULL;
    }
    unsigned char *bytes = malloc((size_t)length + 1);
    if (bytes && fread(bytes, 1, (size_t)length, stream) != (size_t)length) {
        free(bytes);
        bytes = NULL;
    }
    fclose(stream);
    if (bytes) {
        *size = (size_t)length;
        bytes[length] = 0;
    }
    return bytes;
}

static int problem(AvModel *model, const char *message) {
    av_copy(model->error, sizeof(model->error), message);
    return 0;
}

static int add_material(AvModel *model, const char *name) {
    for (size_t i = 0; i < model->material_count; ++i) {
        if (strcmp(model->materials[i].name, name) == 0) return (int)i;
    }
    if (model->material_count >= 4096 || !av_reserve((void **)&model->materials,
            &model->material_capacity, model->material_count + 1, sizeof(AvMaterial))) return -1;
    size_t index = model->material_count++;
    AvMaterial *material = &model->materials[index];
    av_copy(material->name, sizeof(material->name), name);
    material->color = AV_DEFAULT_COLOR;
    material->diffuse = material->light = -1;
    return (int)index;
}

void av_model_free(AvModel *model) {
    for (uint32_t lod = 0; lod < AV_LODS; ++lod) {
        free(model->lods[lod].vertices);
        free(model->lods[lod].triangles);
    }
    for (size_t n = 0; n < model->texture_count; ++n) free(model->textures[n].image.pixels);
    free(model->textures);
    free(model->materials);
    free(model->bmf);
    memset(model, 0, sizeof(*model));
}

void av_mesh_bounds(AvMesh *mesh) {
    if (!mesh->vertex_count) return;
    Vec3 low = mesh->vertices[0].position;
    Vec3 high = low;
    for (size_t i = 1; i < mesh->vertex_count; ++i) {
        Vec3 p = mesh->vertices[i].position;
        if (p.x < low.x) low.x = p.x;
        if (p.y < low.y) low.y = p.y;
        if (p.z < low.z) low.z = p.z;
        if (p.x > high.x) high.x = p.x;
        if (p.y > high.y) high.y = p.y;
        if (p.z > high.z) high.z = p.z;
    }
    mesh->center = av_scale(av_add(low, high), 0.5f);
    Vec3 half = av_scale(av_sub(high, low), 0.5f);
    mesh->radius = sqrtf(av_dot(half, half));
    if (mesh->radius < 1e-6f) mesh->radius = 1.0f;
}

static int grow_mesh(AvMesh *mesh, size_t vertices, size_t triangles) {
    return vertices <= AV_MAX_CORNERS && triangles <= AV_MAX_CORNERS / 3
        && av_reserve((void **)&mesh->vertices, &mesh->vertex_capacity, vertices, sizeof(AvVertex))
        && av_reserve((void **)&mesh->triangles, &mesh->triangle_capacity, triangles, sizeof(AvTriangle));
}

static Vec3 vec3(const unsigned char *p) {
    return (Vec3){av_f32(p), av_f32(p + 4), av_f32(p + 8)};
}

static Vec2 vec2(const unsigned char *p) { return (Vec2){av_f32(p), av_f32(p + 4)}; }

static int valid_position(Vec3 p) {
    return av_isfinite(p.x) && av_isfinite(p.y) && av_isfinite(p.z)
        && fabsf(p.x) <= 1e12f && fabsf(p.y) <= 1e12f && fabsf(p.z) <= 1e12f;
}

static Vec2 safe_uv(Vec2 uv) {
    if (!av_isfinite(uv.x) || !av_isfinite(uv.y)) return (Vec2){0, 0};
    return uv;
}

static int validate_bmf(const unsigned char *bytes, size_t size) {
    if (size < 16) return 0;
    uint32_t frames = av_u32(bytes), triangles = av_u32(bytes + 4);
    uint32_t vertices = av_u32(bytes + 8), uv_count = av_u32(bytes + 12);
    if (!frames || !triangles || !vertices || !uv_count || triangles > AV_MAX_CORNERS / 3
            || vertices > AV_MAX_CORNERS || uv_count > AV_MAX_CORNERS) return 0;
    uint64_t expected = 16 + (uint64_t)triangles * 24 + (uint64_t)uv_count * 8;
    expected += (uint64_t)frames * (28 + (uint64_t)vertices * 12 + (uint64_t)triangles * 88);
    if (expected != size) return 0;
    const unsigned char *faces = bytes + 16;
    for (uint32_t face = 0; face < triangles; ++face, faces += 24) {
        for (unsigned corner = 0; corner < 3; ++corner) {
            if (av_u32(faces + corner * 4) >= vertices || av_u32(faces + 12 + corner * 4) >= uv_count) return 0;
        }
    }
    return 1;
}

int av_model_set_frame(AvModel *model, uint32_t frame) {
    if (model->kind != AV_BMF || frame >= model->frame_count) return 0;
    const unsigned char *bytes = model->bmf;
    uint32_t triangles = av_u32(bytes + 4), vertices = av_u32(bytes + 8), uv_count = av_u32(bytes + 12);
    const unsigned char *faces = bytes + 16;
    const unsigned char *uv_data = faces + (size_t)triangles * 24;
    size_t frame_size = 28 + (size_t)vertices * 12 + (size_t)triangles * 88;
    const unsigned char *positions = uv_data + (size_t)uv_count * 8 + (size_t)frame * frame_size + 28;
    const unsigned char *normals = positions + (size_t)vertices * 12;
    for (uint32_t v = 0; v < vertices; ++v) {
        if (!valid_position(vec3(positions + (size_t)v * 12))) return 0;
    }
    AvMesh *mesh = &model->lods[0];
    for (uint32_t face = 0; face < triangles; ++face, faces += 24) {
        for (unsigned corner = 0; corner < 3; ++corner) {
            uint32_t vi = av_u32(faces + corner * 4), ui = av_u32(faces + 12 + corner * 4);
            size_t expanded = (size_t)face * 3 + corner;
            AvVertex *vertex = &mesh->vertices[expanded];
            vertex->position = vec3(positions + (size_t)vi * 12);
            vertex->normal = av_normal(vec3(normals + expanded * 12));
            vertex->uv = safe_uv(vec2(uv_data + (size_t)ui * 8));
            mesh->triangles[face].vertex[corner] = (uint32_t)expanded;
        }
        mesh->triangles[face].material = 0;
    }
    model->frame = frame;
    return 1;
}

static int parse_bmf(AvModel *model, unsigned char *bytes, size_t size) {
    if (!validate_bmf(bytes, size)) return problem(model, "Unsupported or truncated BMF layout.");
    uint32_t triangles = av_u32(bytes + 4);
    AvMesh *mesh = &model->lods[0];
    if (!grow_mesh(mesh, (size_t)triangles * 3, triangles) || add_material(model, "body") < 0) {
        return problem(model, "Not enough memory for the BMF mesh.");
    }
    mesh->triangle_count = triangles;
    mesh->vertex_count = (size_t)triangles * 3;
    memset(mesh->vertices, 0, mesh->vertex_count * sizeof(AvVertex));
    model->kind = AV_BMF;
    model->lod_count = 1;
    model->bmf = bytes;
    model->bmf_size = size;
    model->frame_count = av_u32(bytes);
    if (!av_model_set_frame(model, 0)) return problem(model, "BMF contains invalid model coordinates.");
    return 1;
}

static Vec3 apply_matrix(Vec3 p, const float matrix[16], int position) {
    Vec3 changed = {
        p.x * matrix[0] + p.y * matrix[4] + p.z * matrix[8],
        p.x * matrix[1] + p.y * matrix[5] + p.z * matrix[9],
        p.x * matrix[2] + p.y * matrix[6] + p.z * matrix[10]
    };
    if (!position) return av_normal(changed);
    return av_add(changed, (Vec3){matrix[12], matrix[13], matrix[14]});
}

static int read_msh_lod(Reader *reader, AvMesh *mesh, const float matrix[16], size_t material_count) {
    (void)next_u32(reader);
    uint32_t vertex_count = next_u32(reader), index_count = next_u32(reader), groups = next_u32(reader);
    (void)next_u32(reader);
    uint32_t channels = next_u32(reader);
    (void)next_u32(reader);
    if (reader->bad || vertex_count > 65536 || index_count % 3 || index_count > AV_MAX_CORNERS
            || groups > 4096 || (channels & ~0x33aU)) return 0;
    const unsigned char *indices = take(reader, (size_t)index_count * 2);
    const unsigned char *positions = take(reader, (size_t)vertex_count * 12);
    const unsigned char *normals = channels & 8 ? take(reader, (size_t)vertex_count * 12) : NULL;
    if (channels & 16) (void)take(reader, (size_t)vertex_count * 12);
    if (channels & 32) (void)take(reader, (size_t)vertex_count * 12);
    const unsigned char *uv = channels & 256 ? take(reader, (size_t)vertex_count * 8) : NULL;
    const unsigned char *light = channels & 512 ? take(reader, (size_t)vertex_count * 8) : NULL;
    if (reader->bad) return 0;
    size_t base_vertex = mesh->vertex_count, base_face = mesh->triangle_count;
    uint32_t faces = index_count / 3;
    if (!grow_mesh(mesh, base_vertex + vertex_count, base_face + faces)) return 0;
    for (uint32_t i = 0; i < vertex_count; ++i) {
        AvVertex *vertex = &mesh->vertices[base_vertex + i];
        vertex->position = apply_matrix(vec3(positions + (size_t)i * 12), matrix, 1);
        if (!valid_position(vertex->position)) return 0;
        vertex->normal = normals ? apply_matrix(vec3(normals + (size_t)i * 12), matrix, 0) : (Vec3){0, 0, 0};
        vertex->uv = uv ? safe_uv(vec2(uv + (size_t)i * 8)) : (Vec2){0, 0};
        vertex->light_uv = light ? safe_uv(vec2(light + (size_t)i * 8)) : (Vec2){0.5f, 0.5f};
    }
    for (uint32_t f = 0; f < faces; ++f) {
        AvTriangle *face = &mesh->triangles[base_face + f];
        face->material = 0;
        for (unsigned c = 0; c < 3; ++c) {
            uint32_t index = av_u16(indices + (size_t)f * 6 + c * 2);
            if (index >= vertex_count) return 0;
            face->vertex[c] = (uint32_t)base_vertex + index;
        }
        if (!normals) {
            AvVertex *v0 = &mesh->vertices[face->vertex[0]], *v1 = &mesh->vertices[face->vertex[1]];
            AvVertex *v2 = &mesh->vertices[face->vertex[2]];
            Vec3 n = av_cross(av_sub(v1->position, v0->position), av_sub(v2->position, v0->position));
            v0->normal = av_add(v0->normal, n);
            v1->normal = av_add(v1->normal, n);
            v2->normal = av_add(v2->normal, n);
        }
    }
    if (!normals) {
        for (uint32_t i = 0; i < vertex_count; ++i) {
            AvVertex *vertex = &mesh->vertices[base_vertex + i];
            vertex->normal = av_normal(vertex->normal);
        }
    }
    for (uint32_t g = 0; g < groups; ++g) {
        const unsigned char *group = take(reader, 12);
        if (!group) return 0;
        uint32_t start = av_u32(group), count = av_u32(group + 4);
        uint32_t material = av_u16(group + 8), palette = av_u16(group + 10);
        if (start % 3 || count % 3 || start > index_count || count > index_count - start
                || material >= material_count || palette > 32767) return 0;
        if (palette && !take(reader, (size_t)palette * 2)) return 0;
        for (uint32_t i = start / 3; i < (start + count) / 3; ++i) {
            mesh->triangles[base_face + i].material = material;
        }
    }
    mesh->vertex_count += vertex_count;
    mesh->triangle_count += faces;
    mesh->has_light_uv |= light != NULL;
    return 1;
}

static int parse_msh(AvModel *model, const unsigned char *bytes, size_t size) {
    Reader reader = {bytes, size, 0, 0};
    const unsigned char *magic = take(&reader, 8);
    if (!magic || memcmp(magic, "B3DMH\0" "10", 8) != 0) return problem(model, "Expected B3DMH 10 MSH.");
    uint32_t material_count = next_u32(&reader), objects = next_u32(&reader), encoded_size = next_u32(&reader);
    if (!material_count || material_count > 4096 || !objects || objects > 10000 || encoded_size != size) {
        return problem(model, "MSH header or declared size is invalid.");
    }
    if (!av_reserve((void **)&model->materials, &model->material_capacity, material_count, sizeof(AvMaterial))) {
        return problem(model, "Not enough memory for MSH materials.");
    }
    for (uint32_t i = 0; i < material_count; ++i) {
        const unsigned char *name = take(&reader, 64);
        char text[65];
        if (!name) return problem(model, "MSH material table is truncated.");
        memcpy(text, name, 64);
        text[64] = 0;
        AvMaterial *mat = &model->materials[model->material_count++];
        av_copy(mat->name, sizeof(mat->name), text);
        mat->color = AV_DEFAULT_COLOR;
        mat->diffuse = mat->light = -1;
    }
    for (uint32_t object = 0; object < objects; ++object) {
        const unsigned char *header = take(&reader, 228);
        if (!header || av_u32(header) != 0) return problem(model, "This MSH object type is not supported.");
        float matrix[16];
        for (unsigned i = 0; i < 16; ++i) matrix[i] = av_f32(header + 140 + i * 4);
        uint32_t lods = next_u32(&reader);
        if (!lods || lods > AV_LODS) return problem(model, "Unsupported MSH LOD count.");
        if (lods > model->lod_count) model->lod_count = lods;
        for (uint32_t lod = 0; lod < lods; ++lod) {
            if (!read_msh_lod(&reader, &model->lods[lod], matrix, material_count)) {
                return problem(model, "Invalid MSH LOD indices, material groups or vertex channels.");
            }
        }
    }
    if (reader.offset != size || !model->lods[0].triangle_count) {
        return problem(model, "MSH is empty or has a trailing block this viewer cannot read.");
    }
    model->kind = AV_MSH;
    return 1;
}

static int white_space(char c) { return c == ' ' || c == '\t' || c == '\r'; }

static char *trim(char *text) {
    while (white_space(*text)) ++text;
    size_t length = strlen(text);
    while (length && white_space(text[length - 1])) text[--length] = 0;
    return text;
}

/* Split mutable text, retaining the unparsed tail for filenames and material names. */
static char *word(char **rest) {
    char *start = *rest;
    while (white_space(*start)) ++start;
    char *end = start;
    while (*end && !white_space(*end)) ++end;
    if (*end) *end++ = 0;
    *rest = end;
    return start;
}

static char *next_line(char **rest) {
    if (!**rest) return NULL;
    char *line = *rest, *end = strchr(line, '\n');
    if (end) { *end = 0; *rest = end + 1; }
    else *rest += strlen(line);
    return line;
}

static int read_float(char **text, float *value) {
    char *end = NULL;
    double input = strtod(*text, &end);
    if (end == *text || !av_isfinite((float)input)) return 0;
    *value = (float)input;
    *text = end;
    return 1;
}

static int obj_coord(char **text, Vec3 *out, unsigned dimensions) {
    return read_float(text, &out->x) && read_float(text, &out->y)
        && (dimensions == 2 || read_float(text, &out->z));
}

typedef struct { Vec3 *data; size_t count, capacity; } VecList;

static int append_vec(VecList *list, Vec3 item) {
    if (list->count >= AV_MAX_CORNERS || !av_reserve((void **)&list->data, &list->capacity,
            list->count + 1, sizeof(Vec3))) return 0;
    list->data[list->count++] = item;
    return 1;
}

static int read_obj_index(char **part, size_t count, size_t *index) {
    char *end = NULL;
    long offset = strtol(*part, &end, 10);
    if (end == *part || offset == 0 || offset > (long)count || offset < -(long)count) return 0;
    *index = offset > 0 ? (size_t)(offset - 1) : (size_t)((long)count + offset);
    *part = end;
    return 1;
}

static int read_obj_corner(char *part, const VecList *p, const VecList *uv, const VecList *n, AvVertex *out) {
    size_t index = 0;
    *out = (AvVertex){0};
    if (!read_obj_index(&part, p->count, &index)) return 0;
    out->position = p->data[index];
    out->light_uv = (Vec2){0.5f, 0.5f};
    if (*part == '/') {
        ++part;
        if (*part != '/' && *part) {
            if (!read_obj_index(&part, uv->count, &index)) return 0;
            out->uv = (Vec2){uv->data[index].x, uv->data[index].y};
        }
    }
    if (*part == '/') {
        ++part;
        if (*part) {
            if (!read_obj_index(&part, n->count, &index)) return 0;
            out->normal = n->data[index];
        }
    }
    return *part == 0;
}

static int parse_obj(AvModel *model, char *bytes, size_t size, int z_up) {
    if (memchr(bytes, '\0', size)) return problem(model, "OBJ must be a text file.");
    VecList positions = {0}, uvs = {0}, normals = {0};
    AvMesh *mesh = &model->lods[0];
    int material = -1, ok = 1;
    char *line;
    if (size >= 3 && memcmp(bytes, "\xef\xbb\xbf", 3) == 0) bytes += 3;
    while (ok && (line = next_line(&bytes))) {
        char *comment = strchr(line, '#');
        if (comment) *comment = 0;
        const char *tag = word(&line);
        if (strcmp(tag, "v") == 0 || strcmp(tag, "vn") == 0 || strcmp(tag, "vt") == 0) {
            Vec3 item = {0};
            int uv = strcmp(tag, "vt") == 0;
            if (!obj_coord(&line, &item, uv ? 2 : 3)) { ok = 0; break; }
            if (!uv && z_up) { float swap = item.y; item.y = item.z; item.z = swap; }
            if (uv) item.y = 1 - item.y;
            if (strcmp(tag, "vn") == 0) ok = append_vec(&normals, av_normal(item));
            else if (uv) ok = append_vec(&uvs, item);
            else ok = valid_position(item) && append_vec(&positions, item);
        } else if (strcmp(tag, "mtllib") == 0 && !model->mtl_ref[0]) {
            av_copy(model->mtl_ref, sizeof(model->mtl_ref), trim(line));
        } else if (strcmp(tag, "usemtl") == 0) {
            material = add_material(model, trim(line));
            ok = material >= 0;
        } else if (strcmp(tag, "f") == 0) {
            if (material < 0) material = add_material(model, "default");
            if (material < 0) { ok = 0; break; }
            AvVertex first, previous, current;
            if (!read_obj_corner(word(&line), &positions, &uvs, &normals, &first)
                    || !read_obj_corner(word(&line), &positions, &uvs, &normals, &previous)) {
                ok = 0;
                break;
            }
            unsigned polygon_triangles = 0;
            char *part;
            while (*(part = word(&line))) {
                if (!read_obj_corner(part, &positions, &uvs, &normals, &current)
                        || !grow_mesh(mesh, mesh->vertex_count + 3, mesh->triangle_count + 1)) {
                    ok = 0;
                    break;
                }
                AvVertex corners[3] = {first, previous, current};
                if (z_up) { corners[1] = current; corners[2] = previous; }
                Vec3 normal = av_normal(av_cross(av_sub(corners[1].position, corners[0].position),
                    av_sub(corners[2].position, corners[0].position)));
                AvTriangle *triangle = &mesh->triangles[mesh->triangle_count++];
                triangle->material = (uint32_t)material;
                for (unsigned j = 0; j < 3; ++j) {
                    if (av_dot(corners[j].normal, corners[j].normal) == 0) corners[j].normal = normal;
                    triangle->vertex[j] = (uint32_t)mesh->vertex_count;
                    mesh->vertices[mesh->vertex_count++] = corners[j];
                }
                previous = current;
                ++polygon_triangles;
            }
            if (!polygon_triangles) ok = 0;
        }
    }
    free(positions.data);
    free(uvs.data);
    free(normals.data);
    if (!ok || !mesh->triangle_count) return problem(model, "OBJ has no readable faces or contains an invalid face index.");
    model->kind = AV_OBJ;
    model->lod_count = 1;
    return 1;
}

static int local_reference(const char *path) {
    return path[0] && !(path[0] == '/' && path[1] == '/')
        && !(path[0] == '\\' && path[1] == '\\') && !strstr(path, "://");
}

static int join_path(char out[AV_PATH], const char *directory, const char *filename) {
    size_t d = strlen(directory), n = strlen(filename);
    if (d + n + 2 > AV_PATH) return 0;
    memcpy(out, directory, d);
    if (d && out[d - 1] != '/' && out[d - 1] != '\\') out[d++] = '/';
    memcpy(out + d, filename, n + 1);
    return 1;
}

static void directory_of(char out[AV_PATH], const char *path) {
    size_t length = (size_t)(av_filename(path) - path);
    if (length >= AV_PATH) length = AV_PATH - 1;
    memmove(out, path, length);
    out[length] = 0;
}

static int file_exists(const char *path) {
    FILE *file = av_fopen(path, "rb");
    if (!file) return 0;
    fclose(file);
    return 1;
}

static int resolve_path(char out[AV_PATH], const char *source, const char *reference, int parents) {
    if (strlen(reference) >= AV_PATH || !local_reference(reference)) return 0;
    char directory[AV_PATH], ref[AV_PATH];
    av_copy(ref, sizeof(ref), reference);
    for (char *p = ref; *p; ++p) if (*p == '\\') *p = '/';
    if (!local_reference(ref)) return 0;
    if (ref[0] == '/' || (ref[0] && ref[1] == ':')) {
        av_copy(out, AV_PATH, ref);
        return file_exists(out);
    }
    directory_of(directory, source);
    for (int i = 0; i <= parents; ++i) {
        if (join_path(out, directory, ref) && file_exists(out)) return 1;
        if (i == 0 && parents && join_path(out, directory, av_filename(ref)) && file_exists(out)) return 1;
        size_t length = strlen(directory);
        if (!length) break;
        if (directory[length - 1] == '/' || directory[length - 1] == '\\') directory[--length] = 0;
        directory[av_filename(directory) - directory] = 0;
    }
    return 0;
}

static int load_texture(AvModel *model, const char *path) {
    if (!local_reference(path)) return -1;
    for (size_t t = 0; t < model->texture_count; ++t) {
        if (av_equals(model->textures[t].path, path)) return (int)t;
    }
    AvImage image = {0};
    int ok = 0;
    const char *extension = av_extension(path);
    if (av_equals(extension, ".dds") || av_equals(extension, ".bmp")) {
        size_t size = 0;
        unsigned char *data = av_read_file(path, &size);
        if (data) {
            ok = av_equals(extension, ".dds") ? av_dds(data, size, &image) : av_bmp(data, size, &image);
            free(data);
        }
    } else if (av_equals(extension, ".png") || av_equals(extension, ".jpg") || av_equals(extension, ".jpeg")) {
        ok = av_platform_image(path, &image);
    }
    if (!ok) return -1;
    size_t bytes = (size_t)image.width * (size_t)image.height * 4;
    if (bytes > 256u * 1024u * 1024u - model->texture_bytes
            || !av_reserve((void **)&model->textures, &model->texture_capacity,
                model->texture_count + 1, sizeof(AvTexture))) {
        free(image.pixels);
        return -1;
    }
    size_t t = model->texture_count++;
    av_copy(model->textures[t].path, sizeof(model->textures[t].path), path);
    model->textures[t].image = image;
    model->texture_bytes += bytes;
    return (int)t;
}

static const char *obj_map_reference(const char *path, const char *directory) {
    /* The texture path and the export directory often share the same prefix. */
    const char *ref = path;
    const char *base = directory;
    while (*base && *ref) {
        char a = *ref == '\\' ? '/' : *ref;
        char b = *base == '\\' ? '/' : *base;
        if (a != b) return path;
        ++ref;
        ++base;
    }
    return !*base && *ref ? ref : path;
}

static int bind_texture(AvModel *model, const char *source, const char *ref) {
    char path[AV_PATH];
    int index = resolve_path(path, source, ref, model->kind == AV_OBJ ? 0 : 5) ? load_texture(model, path) : -1;
    if (index < 0) ++model->missing_textures;
    return index;
}

/* Parse the common OBJ map options before treating the remaining text as a path. */
static char *map_path(char *text) {
    text = trim(text);
    while (*text == '-') {
        char *option = word(&text);
        if (strcmp(option, "-s") == 0 || strcmp(option, "-o") == 0 || strcmp(option, "-t") == 0) {
            for (unsigned i = 0; i < 3; ++i) {
                char *end = NULL;
                (void)strtod(text, &end);
                if (end == text || (*end && !white_space(*end))) break;
                text = trim(end);
            }
        } else {
            (void)word(&text);
            if (strcmp(option, "-mm") == 0) (void)word(&text);
        }
        text = trim(text);
    }
    size_t n = strlen(text);
    if (n > 1 && *text == '"' && text[n - 1] == '"') { text[n - 1] = 0; ++text; }
    return text;
}

static uint32_t rgb(Vec3 f) {
    return 0xff000000U | ((uint32_t)(av_clamp(f.x, 0, 1) * 255) << 16)
        | ((uint32_t)(av_clamp(f.y, 0, 1) * 255) << 8) | (uint32_t)(av_clamp(f.z, 0, 1) * 255);
}

static void load_materials(AvModel *model) {
    char path[AV_PATH];
    int found = 0;
    if (model->mtl_ref[0]) {
        found = resolve_path(path, model->path, map_path(model->mtl_ref), 0);
        if (!found) ++model->missing_textures;
    } else {
        size_t length = (size_t)(av_extension(model->path) - model->path);
        if (length + 5 < sizeof(path)) {
            memcpy(path, model->path, length);
            memcpy(path + length, ".mtl", 5);
            found = file_exists(path);
        }
    }
    if (found) {
        size_t size = 0;
        unsigned char *content = av_read_file(path, &size);
        char *cursor = (char *)content, *line, current[AV_NAME] = "";
        if (content && size >= 3 && memcmp(content, "\xef\xbb\xbf", 3) == 0) cursor += 3;
        while (content && (line = next_line(&cursor))) {
            char *tag = word(&line);
            if (av_equals(tag, "$MATERIAL") || av_equals(tag, "newmtl")) {
                av_copy(current, sizeof(current), trim(line));
                continue;
            }
            int diffuse = av_equals(tag, "$DIFFUSE") || av_equals(tag, "$DIFFUSE_NOMIP") || av_equals(tag, "map_Kd");
            int light = av_equals(tag, "$LIGHT");
            int tint = av_equals(tag, "Kd");
            if (!diffuse && !light && !tint) continue;
            int index = -1;
            uint32_t color = AV_DEFAULT_COLOR;
            int color_ok = 0;
            for (size_t i = 0; i < model->material_count; ++i) {
                AvMaterial *material = &model->materials[i];
                if (!av_equals(current, material->name)) continue;
                if (tint) {
                    Vec3 new_color;
                    if (!color_ok && obj_coord(&line, &new_color, 3)) { color_ok = 1; color = rgb(new_color); }
                    if (color_ok) material->color = color;
                } else {
                    if (index == -1) {
                        int loaded = bind_texture(model, path, map_path(line));
                        index = loaded < 0 ? -2 : loaded;
                    }
                    if (index >= 0 && light) material->light = index;
                    if (index >= 0 && diffuse) material->diffuse = index;
                }
            }
        }
        free(content);
    }
    if (model->kind == AV_BMF && model->materials[0].diffuse < 0) {
        const char *name = av_filename(model->path);
        const char *preferred = av_equals(name, "cockpitlight.bmf") ? "cockpitlight.dds" :
            av_equals(name, "cockpit.bmf") ? "cockpit.dds" : "1.dds";
        model->materials[0].diffuse = bind_texture(model, model->path, preferred);
    }
}

static void bind_bmf_light(AvModel *model) {
    if (!av_equals(av_filename(model->path), "cockpit.bmf")) return;
    char path[AV_PATH];
    if (!resolve_path(path, model->path, "cockpitlight.bmf", 0)) return;
    size_t size = 0;
    unsigned char *light = av_read_file(path, &size);
    const unsigned char *base = model->bmf;
    if (!light || !validate_bmf(light, size) || av_u32(light + 4) != av_u32(base + 4)) {
        free(light);
        return;
    }
    uint32_t faces = av_u32(light + 4), uv_count = av_u32(light + 12);
    const unsigned char *light_faces = light + 16;
    const unsigned char *light_uv = light_faces + (size_t)faces * 24;
    const unsigned char *light_vertices = light_uv + (size_t)uv_count * 8 + 28;
    uint32_t base_vertices = av_u32(base + 8);
    const unsigned char *base_positions = base + 16 + (size_t)faces * 24 + (size_t)av_u32(base + 12) * 8 + 28;
    AvMesh *mesh = &model->lods[0];
    int same = 1;
    for (uint32_t f = 0; same && f < faces; ++f) {
        for (unsigned c = 0; c < 3; ++c) {
            uint32_t v0 = av_u32(base + 16 + (size_t)f * 24 + c * 4);
            uint32_t v1 = av_u32(light_faces + (size_t)f * 24 + c * 4);
            if (v0 >= base_vertices) { same = 0; break; }
            Vec3 p = vec3(base_positions + (size_t)v0 * 12);
            Vec3 q = vec3(light_vertices + (size_t)v1 * 12);
            if (!(fabsf(p.x-q.x) <= 1e-5f && fabsf(p.y-q.y) <= 1e-5f && fabsf(p.z-q.z) <= 1e-5f)) {
                same = 0;
                break;
            }
        }
    }
    if (same) {
        for (uint32_t f = 0; f < faces; ++f) {
            for (unsigned c = 0; c < 3; ++c) {
                uint32_t uv_index = av_u32(light_faces + (size_t)f * 24 + 12 + c * 4);
                mesh->vertices[(size_t)f * 3 + c].light_uv = safe_uv(vec2(light_uv + (size_t)uv_index * 8));
            }
        }
        mesh->has_light_uv = 1;
        if (resolve_path(path, model->path, "cockpitlight.dds", 0)) model->materials[0].light = load_texture(model, path);
    }
    free(light);
}

int av_model_open(AvModel *model, const char *path, int obj_z_up) {
    if (strlen(path) >= sizeof(model->path)) return problem(model, "File path is too long.");
    av_copy(model->path, sizeof(model->path), path);
    size_t size = 0;
    unsigned char *bytes = av_read_file(path, &size);
    if (!bytes) return problem(model, "Could not read the selected file (maximum size: 512 MiB).");
    const char *type = av_extension(path);
    int ok;
    if (av_equals(type, ".bmf")) ok = parse_bmf(model, bytes, size);
    else if (av_equals(type, ".msh")) ok = parse_msh(model, bytes, size);
    else if (av_equals(type, ".obj")) ok = parse_obj(model, (char *)bytes, size, obj_z_up);
    else ok = problem(model, "Pick a BMF, B3DMH 10 MSH, or Wavefront OBJ file.");
    if (model->bmf != bytes) free(bytes);
    if (!ok) return 0;
    if (!model->frame_count) model->frame_count = 1;
    for (unsigned i = 0; i < model->lod_count; ++i) av_mesh_bounds(&model->lods[i]);
    load_materials(model);
    if (model->kind == AV_BMF) bind_bmf_light(model);
    return 1;
}

int av_model_set_diffuse(AvModel *model, unsigned material, const char *path) {
    if (material >= model->material_count) return 0;
    int texture = load_texture(model, path);
    if (texture < 0) return 0;
    model->materials[material].diffuse = texture;
    return 1;
}

int av_model_demo(AvModel *model) {
    /* A simple inspection solid, generated without any game asset. */
    static const Vec3 points[6] = {
        {0, 0.94f, 0}, {-1, 0, 0}, {0, 0, 1.22f}, {1, 0, 0}, {0, 0, -1.22f}, {0, -0.52f, 0}
    };
    static const unsigned faces[8][3] = {
        {0,1,2}, {0,2,3}, {0,3,4}, {0,4,1}, {5,2,1}, {5,3,2}, {5,4,3}, {5,1,4}
    };
    static const uint32_t colors[4] = {0xff759fc1U, 0xff8edcc5U, 0xffd59961U, 0xffb6c4e9U};
    static const char names[][8] = {"Ice", "Mint", "Copper", "Silver"};
    AvMesh *mesh = &model->lods[0];
    if (!grow_mesh(mesh, 24, 8)) return 0;
    for (unsigned i = 0; i < 4; ++i) {
        if (add_material(model, names[i]) < 0) return 0;
        model->materials[i].color = colors[i];
    }
    for (unsigned i = 0; i < 8; ++i) {
        Vec3 p[3] = {points[faces[i][0]], points[faces[i][1]], points[faces[i][2]]};
        Vec3 n = av_normal(av_cross(av_sub(p[1], p[0]), av_sub(p[2], p[0])));
        mesh->triangles[i] = (AvTriangle){{i*3, i*3+1, i*3+2}, i % 4};
        for (unsigned c = 0; c < 3; ++c) {
            Vec2 uv = {(float)c * 0.5f, c == 1 ? 0 : 1};
            mesh->vertices[i*3+c] = (AvVertex){p[c], n, uv, uv};
        }
    }
    mesh->vertex_count = 24;
    mesh->triangle_count = 8;
    mesh->has_light_uv = 1;
    model->kind = AV_DEMO;
    model->lod_count = model->frame_count = 1;
    av_mesh_bounds(mesh);
    return 1;
}

static int write_obj_materials(const AvModel *model, const char *path) {
    char directory[AV_PATH];
    directory_of(directory, path);
    FILE *file = av_fopen(path, "wb");
    if (!file) return 0;
    for (size_t i = 0; i < model->material_count; ++i) {
        const AvMaterial *material = &model->materials[i];
        uint32_t color = material->diffuse < 0 ? material->color : 0xffffffffU;
        fprintf(file, "newmtl material_%u\nKd %.6f %.6f %.6f\nillum 1\n", (unsigned)i,
            ((color >> 16) & 255) / 255.0, ((color >> 8) & 255) / 255.0, (color & 255) / 255.0);
        if (material->diffuse >= 0) {
            const char *texture = model->textures[material->diffuse].path;
            fprintf(file, "map_Kd %s\n", obj_map_reference(texture, directory));
        }
        fputs("\n", file);
    }
    int ok = !ferror(file);
    return fclose(file) == 0 && ok;
}

int av_write_obj(const AvModel *model, const char *path, unsigned lod) {
    if (lod >= model->lod_count) return 0;
    const AvMesh *mesh = &model->lods[lod];
    if (!mesh->triangle_count || strlen(path) + sizeof(".mtl") > AV_PATH) return 0;
    char material_path[AV_PATH];
    size_t path_size = strlen(path);
    memcpy(material_path, path, path_size);
    memcpy(material_path + path_size, ".mtl", sizeof(".mtl"));
    FILE *file = av_fopen(path, "wb");
    if (!file) return 0;
    fputs("# Air Conflicts Workshop. X right, Y forward, Z up. UV0.\n", file);
    fprintf(file, "mtllib %s\n", av_filename(material_path));
    for (size_t i = 0; i < mesh->vertex_count; ++i) {
        const AvVertex *v = &mesh->vertices[i];
        fprintf(file, "v %.9g %.9g %.9g\nvt %.9g %.9g\nvn %.9g %.9g %.9g\n", (double)v->position.x,
            (double)v->position.z, (double)v->position.y, (double)v->uv.x, 1.0 - (double)v->uv.y,
            (double)v->normal.x, (double)v->normal.z, (double)v->normal.y);
    }
    uint32_t material = UINT32_MAX;
    for (size_t i = 0; i < mesh->triangle_count; ++i) {
        const AvTriangle *face = &mesh->triangles[i];
        if (face->material != material) {
            material = face->material;
            fprintf(file, "usemtl material_%u\n", material);
        }
        uint32_t a = face->vertex[0], b = face->vertex[1], c = face->vertex[2];
        Vec3 n = av_cross(av_sub(mesh->vertices[b].position, mesh->vertices[a].position),
            av_sub(mesh->vertices[c].position, mesh->vertices[a].position));
        Vec3 vertex_normals = av_add(mesh->vertices[a].normal, av_add(mesh->vertices[b].normal, mesh->vertices[c].normal));
        if (av_dot(n, vertex_normals) > 0) { uint32_t swap = b; b = c; c = swap; }
        ++a; ++b; ++c;
        fprintf(file, "f %u/%u/%u %u/%u/%u %u/%u/%u\n", a,a,a,b,b,b,c,c,c);
    }
    int ok = !ferror(file);
    return fclose(file) == 0 && ok && write_obj_materials(model, material_path);
}
