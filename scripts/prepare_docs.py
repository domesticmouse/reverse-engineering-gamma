#!/usr/bin/env python3
"""
scripts/prepare_docs.py

Prepares documentation files for MkDocs build:
1. Copies skills from `.agents/skills/` into `docs/skills/`.
2. Generates `index.md` inside each skill directory from `SKILL.md`.
3. Fixes relative paths (e.g. references to docs/ or other skills) so MkDocs links resolve.
4. Generates a comprehensive skills catalog landing page at `docs/skills/index.md`.
5. Copies `PLAN.md` into `docs/PLAN.md` to make it accessible in the site navigation.
6. Can be used as a standalone script or as an MkDocs hook (via `on_pre_build`).
"""

import os
import shutil
import re

REPO_ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
AGENTS_SKILLS_DIR = os.path.join(REPO_ROOT, ".agents", "skills")
DOCS_DIR = os.path.join(REPO_ROOT, "docs")
TARGET_SKILLS_DIR = os.path.join(DOCS_DIR, "skills")


def parse_frontmatter(file_path):
    """Extracts frontmatter metadata and markdown content from a markdown file without external dependencies."""
    with open(file_path, "r", encoding="utf-8") as f:
        text = f.read()

    frontmatter = {}
    content = text
    if text.startswith("---"):
        parts = text.split("---", 2)
        if len(parts) >= 3:
            raw_fm = parts[1]
            content = parts[2]
            
            # Simple line parser for name and description
            current_key = None
            desc_lines = []
            for line in raw_fm.splitlines():
                line_str = line.strip()
                if line_str.startswith("name:"):
                    frontmatter["name"] = line_str.split(":", 1)[1].strip()
                    current_key = "name"
                elif line_str.startswith("description:"):
                    val = line_str.split(":", 1)[1].strip()
                    if val and val not in (">-", ">", "|", "|-"):
                        frontmatter["description"] = val
                    current_key = "description"
                elif current_key == "description" and (line.startswith("  ") or line.startswith("\t")):
                    desc_lines.append(line_str)
                elif line_str and not line.startswith(" "):
                    current_key = None

            if desc_lines and "description" not in frontmatter:
                frontmatter["description"] = " ".join(desc_lines)
            elif desc_lines and "description" in frontmatter:
                frontmatter["description"] += " " + " ".join(desc_lines)

    # Find the first H1 header
    h1_match = re.search(r"^#\s+(.+)$", content, re.MULTILINE)
    title = h1_match.group(1).strip() if h1_match else frontmatter.get("name", "Skill")

    return frontmatter, title, text


def fix_links_for_skills(content):
    """Adjust relative links in skill markdown files to work within `docs/skills/<skill>/`."""
    # Links like ../../../docs/DAISYSP_GUIDE.md from .agents/skills/<skill>/SKILL.md
    # become ../../DAISYSP_GUIDE.md when located at docs/skills/<skill>/index.md
    content = content.replace("../../../docs/", "../../")
    content = content.replace("../../docs/", "../../")
    content = content.replace("../docs/", "../../")
    
    # Links like [PLAN.md](PLAN.md) or [PLAN.md](../../PLAN.md)
    content = re.sub(r'\]\((?:\.\./)*PLAN\.md\)', '](../../PLAN.md)', content)
    
    return content


def fix_links_for_plan(content):
    """Adjust relative links in PLAN.md so they resolve properly within docs/PLAN.md."""
    # Convert .agents/skills/<skill>/SKILL.md -> skills/<skill>/index.md
    content = re.sub(
        r'\(\.agents/skills/([^/]+)/SKILL\.md\)',
        r'(skills/\1/index.md)',
        content
    )
    # Convert .agents/skills/<skill>/... -> skills/<skill>/...
    content = re.sub(
        r'\(\.agents/skills/([^)]+)\)',
        r'(skills/\1)',
        content
    )
    # Convert repo files outside docs (backups/, firmware/, libDaisy/) to full GitHub URLs
    repo_url = "https://github.com/domesticmouse/reverse-engineering-gamma/blob/main"
    for prefix in ["backups", "firmware", "libDaisy"]:
        content = re.sub(
            rf'\({prefix}/([^)]+)\)',
            rf'({repo_url}/{prefix}/\1)',
            content
        )
    return content


def prepare_docs():
    """Main preparation routine."""
    print(f"Preparing documentation in {DOCS_DIR}...")
    
    # 1. Ensure target directory exists and is clean
    if os.path.exists(TARGET_SKILLS_DIR):
        shutil.rmtree(TARGET_SKILLS_DIR)
    os.makedirs(TARGET_SKILLS_DIR, exist_ok=True)

    skills_data = []

    if not os.path.exists(AGENTS_SKILLS_DIR):
        print(f"Warning: No .agents/skills directory found at {AGENTS_SKILLS_DIR}")
        return

    # 2. Process each skill
    skill_folders = sorted(os.listdir(AGENTS_SKILLS_DIR))
    for folder in skill_folders:
        src_skill_dir = os.path.join(AGENTS_SKILLS_DIR, folder)
        if not os.path.isdir(src_skill_dir):
            continue

        skill_md_src = os.path.join(src_skill_dir, "SKILL.md")
        if not os.path.exists(skill_md_src):
            continue

        dest_skill_dir = os.path.join(TARGET_SKILLS_DIR, folder)
        shutil.copytree(
            src_skill_dir,
            dest_skill_dir,
            ignore=shutil.ignore_patterns("__pycache__", "*.pyc")
        )

        frontmatter, title, raw_text = parse_frontmatter(skill_md_src)
        name = frontmatter.get("name", folder)
        description = frontmatter.get("description", "").strip()

        # Check references/
        references = []
        ref_dir = os.path.join(dest_skill_dir, "references")
        if os.path.exists(ref_dir):
            for ref_file in sorted(os.listdir(ref_dir)):
                if ref_file.endswith(".md"):
                    ref_path = os.path.join(ref_dir, ref_file)
                    _, ref_title, _ = parse_frontmatter(ref_path)
                    references.append({
                        "filename": ref_file,
                        "rel_path": f"references/{ref_file}",
                        "title": ref_title
                    })

        skills_data.append({
            "folder": folder,
            "name": name,
            "title": title,
            "description": description,
            "references": references
        })

        # Process all markdown files in dest_skill_dir to fix relative links
        for root, _, files in os.walk(dest_skill_dir):
            for f in files:
                if f.endswith(".md"):
                    f_path = os.path.join(root, f)
                    with open(f_path, "r", encoding="utf-8") as rf:
                        f_text = rf.read()
                    f_text = fix_links_for_skills(f_text)
                    with open(f_path, "w", encoding="utf-8") as wf:
                        wf.write(f_text)

        # In destination, make index.md a copy of SKILL.md
        dest_index_md = os.path.join(dest_skill_dir, "index.md")
        dest_skill_md = os.path.join(dest_skill_dir, "SKILL.md")
        if os.path.exists(dest_skill_md):
            shutil.copyfile(dest_skill_md, dest_index_md)

    # 3. Generate docs/skills/index.md
    skills_index_content = [
        "# Agent Skills Catalog",
        "",
        "This project includes specialized agent skills tailored for reverse-engineering the **this.is.NOISE Gamma Mini Synth** and developing embedded audio firmware on the **Electro-Smith Daisy Seed 2 DFM** (ARM Cortex-M7 @ 480 MHz, STM32H750, PCM3060 stereo codec).",
        "",
        "These skills provide automated runbooks, verified pin definitions, hardware driver patterns, and architecture guides.",
        "",
        "---",
        "",
        "## Available Skills",
        "",
        "| Skill | Description | References & Tools |",
        "| :--- | :--- | :--- |"
    ]

    for item in skills_data:
        skill_link = f"[{item['name']}]({item['folder']}/index.md)"
        desc = item["description"].replace("\n", " ")
        ref_links = []
        for ref in item["references"]:
            ref_links.append(f"[{ref['title']}]({item['folder']}/{ref['rel_path']})")
        
        # Check scripts or resources
        dest_skill_dir = os.path.join(TARGET_SKILLS_DIR, item['folder'])
        scripts_dir = os.path.join(dest_skill_dir, "scripts")
        if os.path.exists(scripts_dir):
            for sf in sorted(os.listdir(scripts_dir)):
                if sf.endswith(".py"):
                    ref_links.append(f"`scripts/{sf}`")

        resources_dir = os.path.join(dest_skill_dir, "resources")
        if os.path.exists(resources_dir):
            for rf in sorted(os.listdir(resources_dir)):
                if rf.endswith(".h"):
                    ref_links.append(f"`resources/{rf}`")

        extra_str = "<br>".join(ref_links) if ref_links else "—"
        skills_index_content.append(f"| **{skill_link}**<br>_{item['title']}_ | {desc} | {extra_str} |")

    skills_index_content.extend([
        "",
        "---",
        "",
        "## Skill Categorization",
        "",
        "### 1. Hardware & Peripherals",
        "- **[gamma-pinout](gamma-pinout/index.md)**: Master pinout mapping, STM32H750 / Daisy Seed pins, electrical configuration, and polarity inversion specifications.",
        "- **[gamma-hardware-controls](gamma-hardware-controls/index.md)**: Drivers and timing rules for 14 tactile switches, rotary encoder, 4 potentiometers, 2 joysticks, and SSD1306 OLED display.",
        "",
        "### 2. Firmware Flashing & Diagnostic Operations",
        "- **[gamma-firmware-flash](gamma-firmware-flash/index.md)**: Safe USB DFU compilation, validation, and flashing runbook using automated bootloader polling.",
        "- **[gamma-firmware-restore](gamma-firmware-restore/index.md)**: Recovery procedures to restore factory firmware or unbrick unresponsive devices.",
        "- **[gamma-usb-connectivity](gamma-usb-connectivity/index.md)**: Deadlock-immune non-blocking USB CDC logging and Python host control.",
        "",
        "### 3. Audio & DSP Frameworks",
        "- **[daisysp-guide](daisysp-guide/index.md)**: Real-time DSP synthesis modules, filters, effects, memory budgets, and production audio recipes.",
        "- **[libdaisy-guide](libdaisy-guide/index.md)**: STM32H7 HAL architecture, memory allocation (AXI SRAM, DTCM), DMA cache coherency, and peripheral drivers.",
        ""
    ])

    with open(os.path.join(TARGET_SKILLS_DIR, "index.md"), "w", encoding="utf-8") as f:
        f.write("\n".join(skills_index_content))

    # 4. Copy and fix PLAN.md into docs/PLAN.md
    plan_src = os.path.join(REPO_ROOT, "PLAN.md")
    plan_dest = os.path.join(DOCS_DIR, "PLAN.md")
    if os.path.exists(plan_src):
        with open(plan_src, "r", encoding="utf-8") as pf:
            p_text = pf.read()
        p_text = fix_links_for_plan(p_text)
        with open(plan_dest, "w", encoding="utf-8") as pf:
            pf.write(p_text)

    print(f"Successfully processed {len(skills_data)} skills into {TARGET_SKILLS_DIR}.")


# Hook for MkDocs (runs on_pre_build)
def on_pre_build(config):
    prepare_docs()


if __name__ == "__main__":
    prepare_docs()
