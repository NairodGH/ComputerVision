import os
from PIL import Image, ImageDraw, ImageTk
import tkinter as tk
from tkinter import Canvas
import glob

# === CONFIG ===
IMAGES_DIR = "train images"
LABELS_DIR = "train labels"
IMG_SIZE = 640                # expected letterboxed size

# === LOAD IMAGES ===
image_files = sorted(glob.glob(os.path.join(IMAGES_DIR, "*.png")))
index = 0

# === TKINTER WINDOW ===
root = tk.Tk()
root.title("Label Viewer")

canvas = Canvas(root, width=IMG_SIZE, height=IMG_SIZE)
canvas.pack()

def draw_image_with_labels(idx):
    canvas.delete("all")
    img_path = image_files[idx]
    lbl_path = os.path.join(LABELS_DIR, os.path.basename(img_path).replace(".png", ".txt"))

    img = Image.open(img_path).convert("RGB")
    draw = ImageDraw.Draw(img)

    if os.path.exists(lbl_path):
        with open(lbl_path, "r") as f:
            for line in f:
                parts = list(map(float, line.strip().split()))
                if len(parts) < 5:
                    continue
                cls, x, y, w, h, *kps = parts
                # convert normalized to pixel coords
                x *= IMG_SIZE
                y *= IMG_SIZE
                w *= IMG_SIZE
                h *= IMG_SIZE
                left = x - w / 2
                top = y - h / 2
                right = x + w / 2
                bottom = y + h / 2

                # draw bounding box
                draw.rectangle([left, top, right, bottom], outline="red", width=2)

                # draw keypoints (if any)
                for i in range(0, len(kps), 2):
                    kx = kps[i] * IMG_SIZE
                    ky = kps[i + 1] * IMG_SIZE
                    r = 3
                    draw.ellipse([kx - r, ky - r, kx + r, ky + r], fill="cyan")

    tk_img = ImageTk.PhotoImage(img.resize((IMG_SIZE, IMG_SIZE)))
    canvas.image = tk_img
    canvas.create_image(0, 0, anchor=tk.NW, image=tk_img)
    root.title(f"{idx+1}/{len(image_files)} — {os.path.basename(img_path)}")

def next_image(event=None):
    global index
    index = (index + 1) % len(image_files)
    draw_image_with_labels(index)

def prev_image(event=None):
    global index
    index = (index - 1) % len(image_files)
    draw_image_with_labels(index)

root.bind("<Right>", next_image)
root.bind("<Left>", prev_image)

draw_image_with_labels(index)
root.mainloop()
