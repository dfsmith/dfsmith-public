import argparse
import os
import base64
import xml.etree.ElementTree as ET
import requests
import cairosvg
from pathlib import Path
from PIL import Image
from concurrent.futures import ThreadPoolExecutor, as_completed
from pypdf import PdfReader, PdfWriter
from playwright.sync_api import sync_playwright

# Base configuration
BASE_URL = "https://bordbuch-online.audi.de/AudiBordbuch/docs/f03006a9-4473-4533-96b3-a5b79d405f40/files/assets/common/"
IMG_URL_TEMPLATE = BASE_URL + "page-html5-substrates/page{:04d}_3.jpg"
TEXT_URL_TEMPLATE = BASE_URL + "page-vectorlayers/{:04d}.svg"
OUTPUT_PDF = "Audi_S5_2010_Manual.pdf"
TEMP_DIR = "temp_audi_pages"

# Create a clean temporary directory for downloading/converting files
os.makedirs(TEMP_DIR, exist_ok=True)


def clean_svg_namespaces(svg_string: str) -> str:
    """Normalize SVG namespace prefixes and emit clean SVG."""
    #ET.register_namespace("", "http://www.w3.org/2000/svg")
    #ET.register_namespace("xlink", "http://www.w3.org/1999/xlink")
    ET.register_namespace("", "http://w3.org")
    ET.register_namespace("xlink", "http://w3.org")
    root = ET.fromstring(svg_string)
    for elem in root.iter():
        if "}" in elem.tag:
            elem.tag = elem.tag.split("}")[-1]

    return ET.tostring(root, encoding="utf-8").decode("utf-8")


def composite_page(jpg_path: str, svg_path: str, output_pdf_path: str) -> None:
    absolute_jpg = Path(jpg_path).resolve()
    absolute_svg = Path(svg_path).resolve()
    absolute_output = Path(output_pdf_path).resolve()

    # 1. Encode JPEG to Base64
    with open(absolute_jpg, "rb") as image_file:
        encoded_string = base64.b64encode(image_file.read()).decode("utf-8")
    jpeg_data_uri = f"data:image/jpeg;base64,{encoded_string}"

    # 2. Extract and Parse SVG through deep structural translation
    with open(absolute_svg, "r", encoding="utf-8") as f:
        raw_svg_content = f.read()

    # Clean the code structurally through the XML element tree
    svg_content = clean_svg_namespaces(raw_svg_content)
    root = ET.fromstring(raw_svg_content)

    # Read embedded dimensions (strip out 'px' text characters if present)
    svg_width = root.get("width", "595.278px").replace("px", "")
    svg_height = root.get("height", "421.218px").replace("px", "")

    # 3. Create the Scaffold Assembly
    html_content = f"""
    <!DOCTYPE html>
    <html>
    <head>
        <meta charset="utf-8">
        <style>
            @page {{ margin: 0; }}
            html, body {{
                margin: 0; padding: 0;
                width: 100%; height: 100%;
                overflow: hidden; background: transparent;
            }}
            .composite-container {{
                position: relative;
                width: {svg_width}px;
                height: {svg_height}px;
            }}
            .background-layer {{
                position: absolute; top: 0; left: 0;
                width: 100%; height: 100%;
                object-fit: fill; z-index: 1;
            }}
            .foreground-layer {{
                position: absolute;
                top: 0; left: 0;
                width: 100%; height: 100%;
                z-index: 2;
            }}
            svg {{
                width: 100%; height: 100%;
                display: block;
                -webkit-print-color-adjust: exact;
            }}
        </style>
    </head>
    <body>
        <div class="composite-container">
            <img class="background-layer" src="{jpeg_data_uri}" alt="Background">
            <div class="foreground-layer">
                {svg_content}
            </div>
        </div>
    </body>
    </html>
    """
    if False:
        with open(absolute_output.with_suffix(".html"), "w", encoding="utf-8") as f:
            f.write(html_content)

    with sync_playwright() as p:
        browser = p.chromium.launch(headless=True)
        page = browser.new_page()
        page.set_content(html_content, wait_until="networkidle")

        page.pdf(
            path=str(absolute_output),
            print_background=True,
            margin={"top": "0", "right": "0", "bottom": "0", "left": "0"},
            width=f"{svg_width}px",  # Forces the PDF bounding box width
            height=f"{svg_height}px",  # Forces the PDF bounding box height
        )
        browser.close()


def is_svg_complete(svg_path: str) -> bool:
    if not os.path.exists(svg_path):
        return False
    try:
        with open(svg_path, "rb") as svg_file:
            content = svg_file.read()
    except OSError:
        return False

    if len(content) < 10:
        return False
    lower = content.lower()
    return b"<svg" in lower and b"</svg" in lower


def is_pdf_complete(pdf_path: str) -> bool:
    return os.path.exists(pdf_path) and os.path.getsize(pdf_path) > 0


def is_jpg_complete(jpg_path: str) -> bool:
    if not os.path.exists(jpg_path):
        return False
    try:
        with Image.open(jpg_path) as img:
            return img.format == "JPEG" and img.size[0] > 0 and img.size[1] > 0
    except Exception:
        return False


def process_page(page_number: int, force_rebuild: bool = False) -> tuple:
    """Download and composite a single page. Returns (page_number, success, error_msg)."""
    if page_number > 400:
        return (page_number, False, "page limit reached")
    formatted_page = f"{page_number:04d}"
    img_url = IMG_URL_TEMPLATE.format(page_number)
    text_url = TEXT_URL_TEMPLATE.format(page_number)

    temp_jpg_path = Path(TEMP_DIR) / f"{formatted_page}.jpg"
    temp_svg_path = Path(TEMP_DIR) / f"{formatted_page}.svg"
    temp_pdf_path = Path(TEMP_DIR) / f"{formatted_page}.pdf"

    try:
        if is_jpg_complete(temp_jpg_path) and is_svg_complete(temp_svg_path):
            if not force_rebuild and is_pdf_complete(temp_pdf_path):
                return (page_number, True, "cached")

            composite_page(str(temp_jpg_path), str(temp_svg_path), str(temp_pdf_path))
            return (page_number, True, "regenerated" if force_rebuild else "created")

        jpg_response = requests.get(img_url, timeout=10)
        if jpg_response.status_code == 404:
            return (page_number, False, "404")
        jpg_response.raise_for_status()
        if not jpg_response.content:
            return (page_number, False, "empty_jpg")

        # Download SVG
        svg_response = requests.get(text_url, timeout=10)
        svg_response.raise_for_status()
        if not svg_response.content:
            return (page_number, False, "empty_svg")

        temp_jpg_path.write_bytes(jpg_response.content)
        temp_svg_path.write_bytes(svg_response.content)

        # Composite JPG background with SVG text overlay and convert to PDF
        composite_page(temp_jpg_path, temp_svg_path, temp_pdf_path)
        return (page_number, True, "downloaded")

    except requests.exceptions.RequestException as e:
        return (page_number, False, f"network: {e}")
    except Exception as e:
        return (page_number, False, f"error: {e}")


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Download and compile the Audi manual PDF."
    )
    parser.add_argument(
        "--force",
        action="store_true",
        help="Regenerate cached page PDFs even if they already exist",
    )
    args = parser.parse_args()

    writer = PdfWriter()
    page_number = 1
    pages_fetched = 0
    consecutive_failures = 0
    MAX_WORKERS = 8

    print("Starting compilation of Audi Digital Manual (8 parallel downloads)...")

    # Submit tasks for parallel processing
    with ThreadPoolExecutor(max_workers=MAX_WORKERS) as executor:
        pending_futures = {}  # future -> page_number
        results_buffer = {}  # page_number -> (success, status)
        lookahead = MAX_WORKERS * 2  # Submit pages ahead of time
        next_page_to_submit = page_number
        highest_written = 0

        # Keep submitting and collecting until we hit consecutive failures threshold
        while True:
            # Fill the pending queue up to the lookahead
            while len(pending_futures) < lookahead:
                future = executor.submit(process_page, next_page_to_submit, args.force)
                pending_futures[future] = next_page_to_submit
                next_page_to_submit += 1

            if not pending_futures:
                break

            # Wait for the next completed future
            for future in as_completed(list(pending_futures.keys())):
                page_num = pending_futures.pop(future)
                try:
                    page_number_result, success, status = future.result()
                except Exception as e:
                    page_number_result = page_num
                    success = False
                    status = f"exception: {e}"

                results_buffer[page_number_result] = (success, status)

                # Attempt to write contiguous pages from buffer in order
                while (highest_written + 1) in results_buffer:
                    pn = highest_written + 1
                    success, status = results_buffer.pop(pn)
                    formatted_page = f"{pn:04d}"
                    temp_pdf_path = os.path.join(TEMP_DIR, f"{formatted_page}.pdf")

                    if success:
                        print(f"Page {formatted_page}... ({status})")
                        try:
                            reader = PdfReader(temp_pdf_path)
                            writer.append(reader)
                            pages_fetched += 1
                            consecutive_failures = 0
                        except Exception as e:
                            print(f"Failed to append {formatted_page}: {e}")
                            consecutive_failures += 1
                    else:
                        print(f"Page {formatted_page}... {status}")
                        # Treat consecutive 404s or errors as termination condition
                        if status == "404":
                            consecutive_failures += 1
                        else:
                            consecutive_failures += 1

                    highest_written = pn

                    if consecutive_failures >= 3:
                        print("Stopping due to 3 consecutive missing/failed pages")
                        # cancel any remaining futures
                        for f in pending_futures:
                            f.cancel()
                        pending_futures.clear()
                        break

                # Break out to refill pending_futures if we've stopped
                if not pending_futures:
                    break

            # If we've reached the termination condition, stop submitting
            if consecutive_failures >= 3:
                break

    # Merge independent page elements into the final document
    if pages_fetched > 0:
        print(
            f"\nStitching all individual vector layers together into: {OUTPUT_PDF}..."
        )
        with open(OUTPUT_PDF, "wb") as output_pdf:
            writer.write(output_pdf)

        # Keep temporary workspace files for resume capability on future runs
        print(f"Temporary files preserved in {TEMP_DIR} for resume capability.")

        print("\nSuccess! Your complete manual has been downloaded and compiled.")
    else:
        print("\nCompilation failed: No valid page layers could be fetched.")


if __name__ == "__main__":
    main()
