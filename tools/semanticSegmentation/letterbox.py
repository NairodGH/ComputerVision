import cv2
import os
import sys

def letterbox_folder(folder_path):
    target_size = 640
    pad_color = (114, 114, 114)
    exts = (".jpg", ".jpeg", ".png", ".bmp")

    for fname in os.listdir(folder_path):
        if not fname.lower().endswith(exts):
            continue

        fpath = os.path.join(folder_path, fname)
        img = cv2.imread(fpath)
        if img is None:
            continue

        h, w = img.shape[:2]

        dh = target_size - h
        dw = target_size - w

        top = dh // 2
        bottom = dh - top
        left = dw // 2
        right = dw - left

        padded = cv2.copyMakeBorder(
            img, top, bottom, left, right,
            cv2.BORDER_CONSTANT, value=pad_color
        )

        cv2.imwrite(fpath, padded)

if __name__ == "__main__":
    if len(sys.argv) != 2:
        print("Usage: python letterbox.py <folder_path>")
        sys.exit(1)

    letterbox_folder(sys.argv[1])
