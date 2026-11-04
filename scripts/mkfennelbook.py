#!/usr/bin/env python3
import html
import os
import re
import sys

import markdown

CHAPTERS = [
    ("setup", "Setting up Fennel (and LÖVE)"),
    ("tutorial", "Getting started with Fennel"),
    ("lua-primer", "Lua primer"),
    ("reference", "Reference"),
    ("macros", "Macros"),
    ("api", "The Fennel API"),
    ("style", "Style guide"),
    ("from-clojure", "Fennel for Clojure users"),
    ("values", "Values of the Fennel project"),
]

STYLE = """
body { max-width: 46em; margin: 1em auto; padding: 0 1em; font-family: sans-serif; line-height: 1.5; color: #222; background: #fdfdf8; }
pre, code { font-family: monospace; background: #f0f0e8; }
pre { padding: 0.6em; overflow: auto; }
nav { margin: 1em 0; padding: 0.4em 0; border-top: 1px solid #ccc; border-bottom: 1px solid #ccc; }
a { color: #1a5fb4; }
"""


def page(title, body, nav):
    return (f"<!DOCTYPE html>\n<html><head><meta charset=\"utf-8\"><title>{html.escape(title)}</title>"
            f"<style>{STYLE}</style></head><body>\n{nav}\n{body}\n{nav}\n</body></html>\n")


def main():
    src, out = sys.argv[1], sys.argv[2]
    os.makedirs(out, exist_ok=True)
    names = [name for name, _ in CHAPTERS]
    for i, (name, title) in enumerate(CHAPTERS):
        with open(os.path.join(src, name + ".md"), encoding="utf-8") as f:
            text = f.read()
        text = re.sub(r"\]\(([a-z-]+)\.md(#[^)]*)?\)", lambda m: f"]({m.group(1)}.html{m.group(2) or ''})", text)
        body = markdown.markdown(text, extensions=["fenced_code", "tables", "toc"])
        links = ['<a href="index.html">Contents</a>']
        if i > 0:
            links.insert(0, f'<a href="{names[i - 1]}.html">Previous</a>')
        if i + 1 < len(names):
            links.append(f'<a href="{names[i + 1]}.html">Next</a>')
        with open(os.path.join(out, name + ".html"), "w", encoding="utf-8") as f:
            f.write(page(title, body, "<nav>" + " | ".join(links) + "</nav>"))
    items = "\n".join(f'<li><a href="{name}.html">{html.escape(title)}</a></li>' for name, title in CHAPTERS)
    body = ("<h1>The Fennel programming language</h1>\n"
            "<p>The documentation of the Fennel project, a Lisp that compiles to Lua. "
            "The setup chapter shows how to make games with Fennel and LÖVE.</p>\n"
            f"<ol>\n{items}\n</ol>\n"
            "<p>From the Fennel project (<a href=\"https://fennel-lang.org\">fennel-lang.org</a>), "
            "released under the MIT license, see /usr/share/licenses/fennel/LICENSE.</p>")
    with open(os.path.join(out, "index.html"), "w", encoding="utf-8") as f:
        f.write(page("The Fennel programming language", body, ""))


if __name__ == "__main__":
    main()
