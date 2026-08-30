import glob, io, os
import zstandard

d = r'd:\project\gpu\tools'
for p in glob.glob(os.path.join(d, 'mingw-w64-ucrt-*.pkg.tar.zst')):
    tar_path = p[:-4]  # strip .zst
    with open(p, 'rb') as src, open(tar_path, 'wb') as dst:
        zstandard.ZstdDecompressor().copy_stream(src, dst)
    print('ok', os.path.basename(tar_path), os.path.getsize(tar_path))
