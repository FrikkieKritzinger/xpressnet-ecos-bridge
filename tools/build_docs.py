"""Builds the OmniConnect user documents (Word .docx) from the Markdown sources in docs/user/.

  python tools/build_docs.py            # all documents
  python tools/build_docs.py quick_start

The Markdown files are the editable source; the .docx files are generated (open them in Word,
tweak if you like, then print to PDF). Supported Markdown: front matter (title/subtitle/toc),
# ## ### headings, paragraphs, - bullets, 1. numbered lists, | tables |, ![caption](path){w=cm},
> callouts (> **Note** ...), **bold**, `code`, <!-- pagebreak -->.
"""
import datetime
import os
import re
import sys

from docx import Document
from docx.enum.section import WD_ORIENT
from docx.enum.table import WD_TABLE_ALIGNMENT
from docx.enum.text import WD_ALIGN_PARAGRAPH, WD_BREAK
from docx.oxml import OxmlElement
from docx.oxml.ns import qn
from docx.shared import Cm, Pt, RGBColor

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "docs", "user")
BLUE = RGBColor(0x0B, 0x7B, 0xC8)
DARK = RGBColor(0x33, 0x33, 0x33)
GREY = RGBColor(0x66, 0x66, 0x66)

DOCS = {
    "quick_start": "OmniConnect_Quick_Start.docx",
    "user_manual": "OmniConnect_User_Manual.docx",
    "assembly_guide": "OmniConnect_Assembly_and_Connection_Guide.docx",
}


def firmware_version():
    text = open(os.path.join(ROOT, "xpressnet_ecos_bridge", "version.h"), encoding="utf-8").read()
    return re.search(r'FIRMWARE_VERSION\s+"([^"]+)"', text).group(1)


def set_cell_shading(cell, hex_fill):
    tc_pr = cell._tc.get_or_add_tcPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), hex_fill)
    tc_pr.append(shd)


def set_para_shading(paragraph, hex_fill):
    p_pr = paragraph._p.get_or_add_pPr()
    shd = OxmlElement("w:shd")
    shd.set(qn("w:val"), "clear")
    shd.set(qn("w:color"), "auto")
    shd.set(qn("w:fill"), hex_fill)
    p_pr.append(shd)


def para_border_left(paragraph, hex_color):
    p_pr = paragraph._p.get_or_add_pPr()
    borders = OxmlElement("w:pBdr")
    left = OxmlElement("w:left")
    left.set(qn("w:val"), "single")
    left.set(qn("w:sz"), "24")
    left.set(qn("w:space"), "6")
    left.set(qn("w:color"), hex_color)
    borders.append(left)
    p_pr.append(borders)


def add_field(run, instruction):
    for kind, text in (("begin", None), (None, instruction), ("separate", None), (None, "1"), ("end", None)):
        if kind:
            el = OxmlElement("w:fldChar")
            el.set(qn("w:fldCharType"), kind)
        elif text == instruction:
            el = OxmlElement("w:instrText")
            el.set(qn("xml:space"), "preserve")
            el.text = instruction
        else:
            el = OxmlElement("w:t")
            el.text = text
        run._r.append(el)


def add_runs(paragraph, text, size=None, color=None, bold=None):
    """**bold** and `code` inline formatting."""
    for part in re.split(r"(\*\*[^*]+\*\*|`[^`]+`)", text):
        if not part:
            continue
        if part.startswith("**") and part.endswith("**"):
            run = paragraph.add_run(part[2:-2])
            run.bold = True
        elif part.startswith("`") and part.endswith("`"):
            run = paragraph.add_run(part[1:-1])
            run.font.name = "Consolas"
            run._element.rPr.rFonts.set(qn("w:eastAsia"), "Consolas")
            run.font.size = Pt((size or 11) - 1)
            run.font.color.rgb = RGBColor(0x1F, 0x4E, 0x79)
            continue
        else:
            run = paragraph.add_run(part)
            if bold:
                run.bold = True
        if size:
            run.font.size = Pt(size)
        if color is not None:
            run.font.color.rgb = color


def new_document(doc_title, version):
    doc = Document()
    section = doc.sections[0]
    section.page_width, section.page_height = Cm(21.0), Cm(29.7)
    section.left_margin = section.right_margin = Cm(2.2)
    section.top_margin, section.bottom_margin = Cm(2.0), Cm(2.0)

    normal = doc.styles["Normal"]
    normal.font.name = "Calibri"
    normal._element.rPr.rFonts.set(qn("w:eastAsia"), "Calibri")
    normal.font.size = Pt(11)
    normal.font.color.rgb = DARK
    normal.paragraph_format.space_after = Pt(6)
    normal.paragraph_format.line_spacing = 1.1

    for name, size, before, after in (("Heading 1", 20, 18, 8), ("Heading 2", 15, 14, 6), ("Heading 3", 12, 10, 4)):
        st = doc.styles[name]
        st.font.name = "Calibri"
        st._element.rPr.rFonts.set(qn("w:eastAsia"), "Calibri")
        st.font.size = Pt(size)
        st.font.bold = True
        st.font.color.rgb = BLUE
        st.paragraph_format.space_before = Pt(before)
        st.paragraph_format.space_after = Pt(after)
        st.paragraph_format.keep_with_next = True

    # footer: title, firmware version, page number
    footer = section.footer.paragraphs[0]
    footer.alignment = WD_ALIGN_PARAGRAPH.CENTER
    run = footer.add_run("OmniConnect - %s - firmware %s - page " % (doc_title, version))
    run.font.size = Pt(8.5)
    run.font.color.rgb = GREY
    page = footer.add_run()
    page.font.size = Pt(8.5)
    page.font.color.rgb = GREY
    add_field(page, "PAGE")
    section.different_first_page_header_footer = True  # no footer on the title page
    return doc


def title_page(doc, meta, version):
    logo = os.path.join(SRC, "images", "logo_title.png")
    if os.path.exists(logo):
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        p.paragraph_format.space_before = Pt(60)
        p.add_run().add_picture(logo, width=Cm(14.5))
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(40)
    r = p.add_run(meta.get("title", "OmniConnect"))
    r.font.size = Pt(30)
    r.bold = True
    r.font.color.rgb = BLUE
    if meta.get("subtitle"):
        p = doc.add_paragraph()
        p.alignment = WD_ALIGN_PARAGRAPH.CENTER
        r = p.add_run(meta["subtitle"])
        r.font.size = Pt(14)
        r.font.color.rgb = GREY
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.space_before = Pt(60)
    r = p.add_run("Firmware version %s   |   %s" % (version, datetime.date.today().strftime("%B %Y")))
    r.font.size = Pt(11)
    r.font.color.rgb = GREY
    doc.add_page_break()


def add_toc(doc):
    p = doc.add_paragraph()
    r = p.add_run("Contents")
    r.bold = True
    r.font.size = Pt(18)
    r.font.color.rgb = BLUE
    p = doc.add_paragraph()
    add_field(p.add_run(), 'TOC \\o "1-2" \\h \\z \\u')
    note = doc.add_paragraph()
    nr = note.add_run("(In Word: right-click the contents and choose Update Field to refresh page numbers.)")
    nr.italic = True
    nr.font.size = Pt(8.5)
    nr.font.color.rgb = GREY
    doc.add_page_break()


def add_table(doc, rows):
    header = rows[0]
    table = doc.add_table(rows=1, cols=len(header))
    table.style = "Table Grid"
    table.alignment = WD_TABLE_ALIGNMENT.CENTER
    for i, text in enumerate(header):
        cell = table.rows[0].cells[i]
        cell.text = ""
        add_runs(cell.paragraphs[0], text, size=10, color=RGBColor(0xFF, 0xFF, 0xFF), bold=True)
        set_cell_shading(cell, "0B7BC8")
    for row in rows[1:]:
        cells = table.add_row().cells
        for i, text in enumerate(row):
            cells[i].text = ""
            add_runs(cells[i].paragraphs[0], text, size=10)
    doc.add_paragraph().paragraph_format.space_after = Pt(2)


def add_callout(doc, text):
    kind = "note"
    m = re.match(r"\*\*(Note|Tip|Warning|Important|Caution)\*\*[:.]?\s*(.*)", text, re.S)
    if m:
        kind = m.group(1).lower()
    fill, edge = {"warning": ("FDECEA", "C0392B"), "caution": ("FDECEA", "C0392B"),
                  "important": ("FFF4E0", "E08A00"), "tip": ("E8F6EC", "2E8B57")}.get(kind, ("E8F3FB", "0B7BC8"))
    p = doc.add_paragraph()
    p.paragraph_format.left_indent = Cm(0.3)
    p.paragraph_format.space_before = Pt(4)
    p.paragraph_format.space_after = Pt(8)
    set_para_shading(p, fill)
    para_border_left(p, edge)
    add_runs(p, text)


def add_image(doc, caption, path, width_cm):
    full = os.path.join(SRC, path)
    if not os.path.exists(full):
        p = doc.add_paragraph("[missing image: %s]" % path)
        p.runs[0].font.color.rgb = RGBColor(0xC0, 0x39, 0x2B)
        return
    p = doc.add_paragraph()
    p.alignment = WD_ALIGN_PARAGRAPH.CENTER
    p.paragraph_format.keep_with_next = True
    p.paragraph_format.space_after = Pt(2)
    p.add_run().add_picture(full, width=Cm(width_cm))
    if caption:
        c = doc.add_paragraph()
        c.alignment = WD_ALIGN_PARAGRAPH.CENTER
        r = c.add_run(caption)
        r.italic = True
        r.font.size = Pt(9)
        r.font.color.rgb = GREY


def parse_meta(text):
    meta = {}
    if text.startswith("---"):
        end = text.index("\n---", 3)
        for line in text[3:end].strip().splitlines():
            if ":" in line:
                k, v = line.split(":", 1)
                meta[k.strip()] = v.strip()
        text = text[end + 4:].lstrip("\n")
    return meta, text


def build(name):
    src = os.path.join(SRC, name + ".md")
    meta, text = parse_meta(open(src, encoding="utf-8").read())
    version = firmware_version()
    doc = new_document(meta.get("short", meta.get("title", name)), version)
    title_page(doc, meta, version)
    if meta.get("toc", "no") == "yes":
        add_toc(doc)

    lines = text.split("\n")
    i = 0
    while i < len(lines):
        line = lines[i]
        s = line.strip()
        if not s:
            i += 1
            continue
        if s == "<!-- pagebreak -->":
            doc.add_page_break()
        elif s.startswith("### "):
            doc.add_heading(s[4:], level=3)
        elif s.startswith("## "):
            doc.add_heading(s[3:], level=2)
        elif s.startswith("# "):
            doc.add_heading(s[2:], level=1)
        elif s.startswith("!["):
            m = re.match(r"!\[(.*?)\]\((.*?)\)(?:\{w=([\d.]+)\})?", s)
            add_image(doc, m.group(1), m.group(2), float(m.group(3) or 12))
        elif s.startswith("|"):
            rows = []
            while i < len(lines) and lines[i].strip().startswith("|"):
                cells = [c.strip() for c in lines[i].strip().strip("|").split("|")]
                if not all(re.fullmatch(r":?-{2,}:?", c) for c in cells):
                    rows.append(cells)
                i += 1
            add_table(doc, rows)
            continue
        elif s.startswith(">"):
            block = []
            while i < len(lines) and lines[i].strip().startswith(">"):
                block.append(lines[i].strip().lstrip(">").strip())
                i += 1
            add_callout(doc, " ".join(block))
            continue
        elif re.match(r"^[-*] ", s):
            p = doc.add_paragraph(style="List Bullet")
            p.paragraph_format.space_after = Pt(3)
            add_runs(p, s[2:])
        elif re.match(r"^\d+\. ", s):
            p = doc.add_paragraph(style="List Number")
            p.paragraph_format.space_after = Pt(3)
            add_runs(p, re.sub(r"^\d+\.\s+", "", s))
        else:
            para = [s]
            i += 1
            while i < len(lines) and lines[i].strip() and not re.match(r"^(#|\||>|!\[|[-*] |\d+\. |<!--)", lines[i].strip()):
                para.append(lines[i].strip())
                i += 1
            add_runs(doc.add_paragraph(), " ".join(para))
            continue
        i += 1

    out = os.path.join(SRC, DOCS[name])
    doc.save(out)
    print("wrote", os.path.relpath(out, ROOT))


if __name__ == "__main__":
    targets = sys.argv[1:] or list(DOCS)
    for t in targets:
        if os.path.exists(os.path.join(SRC, t + ".md")):
            build(t)
        else:
            print("skipped (no source yet):", t)
