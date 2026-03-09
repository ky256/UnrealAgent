from PIL import Image
import numpy as np
import os

files = {
    'scene': 'I:/Aura/Saved/UnrealAgent/Screenshots/screenshot_20260309_130333.png',
    'viewport': 'I:/Aura/Saved/UnrealAgent/Screenshots/screenshot_20260309_130341.png'
}

for mode, path in files.items():
    img = Image.open(path)
    arr = np.array(img)[:, :, :3]
    gray = np.mean(arr, axis=2)
    avg_brightness = float(np.mean(gray))
    std_dev = float(np.std(gray))
    file_size = os.path.getsize(path)

    # 采样 10k 像素检查唯一色数
    flat = arr.reshape(-1, 3)
    sample = flat[np.random.choice(len(flat), min(10000, len(flat)), replace=False)]
    unique_colors = len(set(map(tuple, sample.tolist())))

    is_black = avg_brightness < 3.0 and unique_colors <= 5

    print(f"=== {mode.upper()} ===")
    print(f"  Resolution: {img.size[0]}x{img.size[1]}")
    print(f"  File size: {file_size / 1024:.1f} KB")
    print(f"  Avg brightness: {avg_brightness:.1f} / 255")
    print(f"  Std deviation: {std_dev:.1f}")
    print(f"  Unique colors (10k sample): {unique_colors}")
    print(f"  Is black screen: {is_black}")
    print()

# 结论
scene_bright = float(np.mean(np.array(Image.open(files['scene']))[:,:,:3]))
viewport_bright = float(np.mean(np.array(Image.open(files['viewport']))[:,:,:3]))
print(f"=== COMPARISON ===")
print(f"  Scene brightness:   {scene_bright:.1f}")
print(f"  Viewport brightness: {viewport_bright:.1f}")
print(f"  Ratio: {viewport_bright/max(scene_bright, 0.01):.2f}")
if viewport_bright > 3.0:
    print(f"  VERDICT: viewport is NOT black - BUG FIXED!")
else:
    print(f"  VERDICT: viewport is still black - BUG NOT FIXED")
