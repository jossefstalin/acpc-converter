"""Exercise the Windows executable with original, synthetic model fixtures."""

from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import zlib


sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import ac_formats


def fixture_png(path):
    def chunk(kind, data):
        content = kind + data
        return struct.pack(">I", len(data)) + content + struct.pack(">I", zlib.crc32(content))

    header = struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header)
                     + chunk(b"IDAT", zlib.compress(b"\0\xff\0\0")) + chunk(b"IEND", b""))


def fixtures(folder):
    texture = folder / "color.png"
    fixture_png(texture)
    obj = folder / "预览 模型.obj"
    obj.write_text(
        "mtllib test.mtl\nv 0 0 0\nv 1 0 0\nv 0 0 1\n"
        "vt 0 0\nvt 1 0\nvt 0 1\nusemtl test\nf -3/-3 -2/-2 -1/-1\n",
        encoding="utf-8",
    )
    (folder / "test.mtl").write_text("newmtl test\nmap_Kd color.png\n", encoding="utf-8")
    vertices = {
        "position": [(0, 0, 0), (1, 0, 0), (0, 1, 0)],
        "normal": [(0, 0, 1)] * 3,
        "tangent": [(1, 0, 0)] * 3,
        "bitangent": [(0, 1, 0)] * 3,
        "uv0": [(0, 0), (1, 0), (0, 1)],
        "uv1": [(0.5, 0.5), (0.5, 0.1), (0.1, 0.5)],
    }
    triangle = ac_formats.Lod([0, 2, 1], vertices, [(0, 3, 0, ())])
    bmf = folder / "model.bmf"
    bmf.write_bytes(ac_formats.lod_to_bmf(triangle).to_bytes())
    msh = folder / "model.msh"
    msh.write_bytes(ac_formats.make_msh(triangle, ["test"]).to_bytes())
    (folder / "model.mtl").write_text("$MATERIAL test\n$DIFFUSE color.png\n", encoding="utf-8")
    return [(obj, True), (bmf, False), (msh, True)]


def run(exe):
    with tempfile.TemporaryDirectory(prefix="acpc-viewer-") as directory:
        folder = Path(directory)
        cases = [(None, False), *fixtures(folder)]
        for index, (model, expect_red) in enumerate(cases):
            output = folder / f"preview-{index}.bmp"
            command = [str(exe), "--smoke-test", str(output)]
            if model:
                command.append(str(model))
            subprocess.run(command, check=True, timeout=25)
            bitmap = output.read_bytes()
            assert bitmap[:2] == b"BM" and len(bitmap) > 54
            assert struct.unpack_from("<I", bitmap, 2)[0] == len(bitmap)
            if expect_red:
                assert any(r > 113 and g == b == 0 for b, g, r in zip(
                    bitmap[54::4], bitmap[55::4], bitmap[56::4],
                )), "The PNG texture was not visible in the preview."
            print(f"Rendered {'internal sample' if model is None else model.suffix}")


if __name__ == "__main__":
    run(Path(sys.argv[1]).resolve(strict=True))
