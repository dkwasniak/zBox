#!/usr/bin/env python3
# Usage:
#   pip install Pillow reportlab
#
#   Osobne PDFy (jeden plik = jedna karta):
#     python process_cards.py <input_folder> <output_folder>
#
#   Jeden PDF ze wszystkimi kartami (każda na osobnej stronie A4):
#     python process_cards.py <input_folder> <output.pdf> --merge

import argparse
import io
import sys
from collections import deque
from pathlib import Path

from PIL import Image, ImageDraw
from reportlab.lib.pagesizes import A4
from reportlab.lib.units import mm
from reportlab.pdfgen import canvas
from reportlab.lib.utils import ImageReader


# Fizyczne wymiary karty CR80 (standard ISO/IEC 7810 ID-1)
CARD_W_MM = 54.15
CARD_H_MM = 85.70
CARD_RATIO = CARD_W_MM / CARD_H_MM  # ~0.6309
CORNER_RADIUS_MM = 3.0
DPI = 300

# PDF zawiera dokładne wymiary fizyczne — drukarnia drukuje 1:1.
# Domowy wydruk testowy: ustaw "Rzeczywisty rozmiar" / "Actual Size" / 100% w oknie druku.
CARD_W_PDF_MM = CARD_W_MM
CARD_H_PDF_MM = CARD_H_MM


def clean_background(img: Image.Image, tolerance: int = 25) -> Image.Image:
    """BFS flood-fill od narożników — zastępuje tło czystą bielą."""
    img = img.convert("RGB")
    pixels = img.load()
    w, h = img.size
    visited = bytearray(w * h)  # flat bool array

    queue = deque()
    for sx, sy in [(0, 0), (w - 1, 0), (0, h - 1), (w - 1, h - 1)]:
        idx = sy * w + sx
        if not visited[idx]:
            r, g, b = pixels[sx, sy]
            if r >= 255 - tolerance and g >= 255 - tolerance and b >= 255 - tolerance:
                visited[idx] = 1
                queue.append((sx, sy))

    while queue:
        x, y = queue.popleft()
        pixels[x, y] = (255, 255, 255)
        for nx, ny in ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)):
            if 0 <= nx < w and 0 <= ny < h:
                idx = ny * w + nx
                if not visited[idx]:
                    r, g, b = pixels[nx, ny]
                    if r >= 255 - tolerance and g >= 255 - tolerance and b >= 255 - tolerance:
                        visited[idx] = 1
                        queue.append((nx, ny))
    return img


def find_content_bbox(img: Image.Image, white_threshold: int = 240) -> tuple[int, int, int, int]:
    grayscale = img.convert("L")
    pixels = grayscale.load()
    w, h = img.size
    left, top, right, bottom = w, h, 0, 0
    found = False
    for y in range(h):
        for x in range(w):
            if pixels[x, y] < white_threshold:
                if x < left:
                    left = x
                if x > right:
                    right = x
                if y < top:
                    top = y
                if y > bottom:
                    bottom = y
                found = True
    if not found:
        return (0, 0, w, h)
    return (left, top, right + 1, bottom + 1)


def smart_crop_to_ratio(img: Image.Image, target_ratio: float) -> Image.Image:
    w, h = img.size
    target_w = int(h * target_ratio)
    if target_w >= w:
        return img
    left_b, _, right_b, _ = find_content_bbox(img)
    content_cx = (left_b + right_b) // 2
    crop_left = content_cx - target_w // 2
    crop_right = crop_left + target_w
    if crop_left < 0:
        crop_left = 0
        crop_right = target_w
    if crop_right > w:
        crop_right = w
        crop_left = w - target_w
    return img.crop((crop_left, 0, crop_right, h))


def round_corners(img: Image.Image, radius_px: int) -> Image.Image:
    result = Image.new("RGB", img.size, (255, 255, 255))
    mask = Image.new("L", img.size, 0)
    draw = ImageDraw.Draw(mask)
    draw.rounded_rectangle([(0, 0), (img.width - 1, img.height - 1)], radius=radius_px, fill=255)
    result.paste(img, mask=mask)
    return result


def prepare_card(png_path: Path, card_h_px: int, corner_radius_px: int) -> Image.Image:
    img = Image.open(png_path).convert("RGB")
    img = clean_background(img)
    img = smart_crop_to_ratio(img, CARD_RATIO)
    target_w = int(card_h_px * CARD_RATIO)
    img = img.resize((target_w, card_h_px), Image.LANCZOS)
    img = round_corners(img, corner_radius_px)
    return img


def draw_card_outline(c: canvas.Canvas, x_pt: float, y_pt: float, card_w_pt: float, card_h_pt: float) -> None:
    corner_r_pt = CORNER_RADIUS_MM * mm
    c.setStrokeColorRGB(0, 0, 0)
    c.setLineWidth(0.5)
    p = c.beginPath()
    p.moveTo(x_pt + corner_r_pt, y_pt)
    p.lineTo(x_pt + card_w_pt - corner_r_pt, y_pt)
    p.arcTo(x_pt + card_w_pt - 2*corner_r_pt, y_pt,
            x_pt + card_w_pt, y_pt + 2*corner_r_pt, startAng=-90, extent=90)
    p.lineTo(x_pt + card_w_pt, y_pt + card_h_pt - corner_r_pt)
    p.arcTo(x_pt + card_w_pt - 2*corner_r_pt, y_pt + card_h_pt - 2*corner_r_pt,
            x_pt + card_w_pt, y_pt + card_h_pt, startAng=0, extent=90)
    p.lineTo(x_pt + corner_r_pt, y_pt + card_h_pt)
    p.arcTo(x_pt, y_pt + card_h_pt - 2*corner_r_pt,
            x_pt + 2*corner_r_pt, y_pt + card_h_pt, startAng=90, extent=90)
    p.lineTo(x_pt, y_pt + corner_r_pt)
    p.arcTo(x_pt, y_pt,
            x_pt + 2*corner_r_pt, y_pt + 2*corner_r_pt, startAng=180, extent=90)
    p.close()
    c.drawPath(p, stroke=1, fill=0)


def draw_card_on_canvas(c: canvas.Canvas, img: Image.Image, outline: bool = False) -> None:
    card_w_pt = CARD_W_PDF_MM * mm
    card_h_pt = CARD_H_PDF_MM * mm
    a4_w_pt, a4_h_pt = A4
    x_pt = (a4_w_pt - card_w_pt) / 2
    y_pt = (a4_h_pt - card_h_pt) / 2

    c.setFillColorRGB(1, 1, 1)
    c.rect(0, 0, a4_w_pt, a4_h_pt, fill=1, stroke=0)

    buf = io.BytesIO()
    img.save(buf, format="PNG")
    buf.seek(0)
    c.drawImage(ImageReader(buf), x_pt, y_pt, width=card_w_pt, height=card_h_pt, preserveAspectRatio=False)

    if outline:
        draw_card_outline(c, x_pt, y_pt, card_w_pt, card_h_pt)


def draw_outline_page(c: canvas.Canvas) -> None:
    """Blank page with a card-sized black outline — for physical size verification."""
    card_w_pt = CARD_W_PDF_MM * mm
    card_h_pt = CARD_H_PDF_MM * mm
    a4_w_pt, a4_h_pt = A4
    x_pt = (a4_w_pt - card_w_pt) / 2
    y_pt = (a4_h_pt - card_h_pt) / 2

    c.setFillColorRGB(1, 1, 1)
    c.rect(0, 0, a4_w_pt, a4_h_pt, fill=1, stroke=0)
    draw_card_outline(c, x_pt, y_pt, card_w_pt, card_h_pt)

    # Dimension labels
    c.setFont("Helvetica", 7)
    c.setFillColorRGB(0.4, 0.4, 0.4)
    c.drawCentredString(a4_w_pt / 2, y_pt - 10, f"{CARD_W_MM:.2f} mm")
    c.drawString(x_pt + card_w_pt + 6, y_pt + card_h_pt / 2, f"{CARD_H_MM:.2f} mm")
    c.drawCentredString(a4_w_pt / 2, y_pt - 20, "CR80 portrait — wydrukuj w 100% / Actual Size (nie 'dopasuj do strony')")


def process_merged(png_files: list[Path], output_pdf: Path, card_h_px: int, corner_radius_px: int) -> None:
    total = len(png_files)
    c = canvas.Canvas(str(output_pdf), pagesize=A4)

    # Page 1: outline template for physical size check
    print("Generating outline test page (strona 1)...")
    draw_outline_page(c)
    c.showPage()

    for i, png_path in enumerate(png_files, 1):
        print(f"Processing {i}/{total}: {png_path.name}")
        img = prepare_card(png_path, card_h_px, corner_radius_px)
        draw_card_on_canvas(c, img, outline=True)
        c.showPage()

    c.save()
    print(f"\nGotowe. 1 strona testowa + {total} kart = {total + 1} stron w: {output_pdf}")


def process_individual(png_files: list[Path], output_dir: Path, card_h_px: int, corner_radius_px: int) -> None:
    output_dir.mkdir(parents=True, exist_ok=True)
    total = len(png_files)

    for i, png_path in enumerate(png_files, 1):
        print(f"Processing {i}/{total}: {png_path.name}")
        img = prepare_card(png_path, card_h_px, corner_radius_px)
        out_path = output_dir / (png_path.stem + ".pdf")
        c = canvas.Canvas(str(out_path), pagesize=A4)
        draw_card_on_canvas(c, img)
        c.save()

    print(f"\nGotowe. {total} PDF(ów) w: {output_dir}")


def main():
    parser = argparse.ArgumentParser(description="Convert card PNGs to A4 PDFs (CR80 portrait, 300 DPI)")
    parser.add_argument("input", type=Path, help="Folder z plikami PNG")
    parser.add_argument("output", type=Path, help="Folder wyjściowy (lub plik .pdf przy --merge)")
    parser.add_argument("--merge", action="store_true", help="Wszystkie karty w jednym PDF (każda na osobnej stronie)")
    args = parser.parse_args()

    if not args.input.is_dir():
        print(f"Błąd: folder nie istnieje: {args.input}")
        sys.exit(1)

    png_files = sorted(args.input.glob("*.png"))
    # exclude process_cards.py (not a png, just safety)
    if not png_files:
        print("Brak plików PNG w folderze.")
        sys.exit(1)

    card_h_px = int(CARD_H_MM / 25.4 * DPI)       # 86 mm @ 300 DPI ≈ 1016 px
    corner_radius_px = int(CORNER_RADIUS_MM / CARD_H_MM * card_h_px)  # ≈ 35 px

    if args.merge:
        output_pdf = args.output
        if output_pdf.suffix.lower() != ".pdf":
            output_pdf = output_pdf.with_suffix(".pdf")
        output_pdf.parent.mkdir(parents=True, exist_ok=True)
        process_merged(png_files, output_pdf, card_h_px, corner_radius_px)
    else:
        process_individual(png_files, args.output, card_h_px, corner_radius_px)


if __name__ == "__main__":
    main()
