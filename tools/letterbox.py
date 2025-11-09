import os
import cv2
import numpy as np
from glob import glob

INPUT_SIZE = (640, 640)
ORIG_SIZE = (640, 480)
PADDING_COLOR = (114, 114, 114)

# paths
IMG_DIRS = [os.path.join("images", "train"), os.path.join("images", "val")]
LBL_DIRS = [os.path.join("labels", "train"), os.path.join("labels", "val")]

def letterbox_image(img):
    """Resize 640x480 → 640x640 with gray padding top/bottom."""
    h, w = img.shape[:2]
    target_w, target_h = INPUT_SIZE
    scale = min(target_w / w, target_h / h)
    new_w, new_h = int(w * scale), int(h * scale)

    resized = cv2.resize(img, (new_w, new_h), interpolation=cv2.INTER_LINEAR)

    pad_h = target_h - new_h
    pad_top = pad_h // 2
    pad_bottom = pad_h - pad_top

    padded = cv2.copyMakeBorder(resized, pad_top, pad_bottom, 0, 0, cv2.BORDER_CONSTANT, value=PADDING_COLOR)
    return padded, scale, pad_top

def adjust_label_line(line, scale, pad_top):
    parts = line.strip().split()
    if len(parts) < 5:
        return None  # invalid line

    cls = parts[0]
    floats = list(map(float, parts[1:]))

    x, y, w, h = floats[:4]
    # denormalize (original 640x480)
    x *= ORIG_SIZE[0]
    y *= ORIG_SIZE[1]
    w *= ORIG_SIZE[0]
    h *= ORIG_SIZE[1]

    # letterbox scaling
    x *= scale
    y *= scale
    w *= scale
    h *= scale
    y += pad_top

    # normalize back to 640x640
    x /= INPUT_SIZE[0]
    y /= INPUT_SIZE[1]
    w /= INPUT_SIZE[0]
    h /= INPUT_SIZE[1]

    new_floats = [x, y, w, h]

    # process keypoints (if any)
    if len(floats) > 4:
        kps = floats[4:]
        new_kps = []
        for i in range(0, len(kps), 2):
            kx = kps[i] * ORIG_SIZE[0]
            ky = kps[i + 1] * ORIG_SIZE[1]
            kx = kx * scale / INPUT_SIZE[0]
            ky = (ky * scale + pad_top) / INPUT_SIZE[1]
            new_kps += [kx, ky]
        new_floats += new_kps

    return cls + " " + " ".join(f"{v:.6f}" for v in new_floats)

def process_set(img_dir, lbl_dir):
    os.makedirs(img_dir, exist_ok=True)
    os.makedirs(lbl_dir, exist_ok=True)
    img_paths = sorted(glob(os.path.join(img_dir, "*.png")) + glob(os.path.join(img_dir, "*.jpg")))

    for img_path in img_paths:
        base = os.path.splitext(os.path.basename(img_path))[0]
        lbl_path = os.path.join(lbl_dir, base + ".txt")
        if not os.path.exists(lbl_path):
            continue

        img = cv2.imread(img_path)
        padded, scale, pad_top = letterbox_image(img)

        # overwrite image
        cv2.imwrite(img_path, padded)

        with open(lbl_path, "r") as f:
            lines = f.readlines()

        new_lines = []
        for line in lines:
            adj = adjust_label_line(line, scale, pad_top)
            if adj:
                new_lines.append(adj)

        with open(lbl_path, "w") as f:
            f.write("\n".join(new_lines))

        print(f"Processed {base}")

if __name__ == "__main__":
    for img_dir, lbl_dir in zip(IMG_DIRS, LBL_DIRS):
        process_set(img_dir, lbl_dir)
    print("Done.")