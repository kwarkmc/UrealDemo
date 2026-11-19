"""Collect factual identifiers/units/flags from the public Prepar3D v6 SDK."""
import csv
import hashlib
import json
from collections import Counter
from datetime import datetime, timezone
from html.parser import HTMLParser
from pathlib import Path
from urllib.parse import urljoin
from urllib.request import urlopen


class Node:
    def __init__(self, tag="", attrs=()):
        self.tag, self.attrs, self.children = tag, dict(attrs), []

    def text(self):
        return " ".join(" ".join(c.text() if isinstance(c, Node) else c
                                 for c in self.children).split())

    def walk(self, tag=None):
        for child in self.children:
            if isinstance(child, Node):
                if tag is None or child.tag == tag:
                    yield child
                yield from child.walk(tag)


class Document(HTMLParser):
    def __init__(self, html):
        super().__init__(convert_charrefs=True)
        self.root = Node()
        self.stack = [self.root]
        self.feed(html)

    def handle_starttag(self, tag, attrs):
        node = Node(tag, attrs)
        self.stack[-1].children.append(node)
        if tag not in {"area", "base", "br", "col", "embed", "hr", "img", "input",
                       "link", "meta", "param", "source", "track", "wbr"}:
            self.stack.append(node)

    def handle_endtag(self, tag):
        for i in range(len(self.stack) - 1, 0, -1):
            if self.stack[i].tag == tag:
                del self.stack[i:]
                break

    def handle_data(self, data):
        self.stack[-1].children.append(data)


BASE = "https://www.prepar3d.com/SDKv6/sdk/"
OUT = Path(__file__).resolve().parents[1] / "docs" / "simconnect-data"


def collect():
    OUT.mkdir(exist_ok=True)
    sources = []

    def fetch(path):
        url = urljoin(BASE, path)
        with urlopen(url, timeout=45) as response:
            raw = response.read()
        sources.append({"url": url, "bytes": len(raw),
                        "sha256": hashlib.sha256(raw).hexdigest()})
        return Document(raw.decode("utf-8-sig")).root, url

    def save(name, rows, fields):
        assert rows, name
        with (OUT / (name + ".csv")).open("w", encoding="utf-8-sig", newline="") as f:
            writer = csv.DictWriter(f, fieldnames=fields)
            writer.writeheader()
            writer.writerows(rows)

    variables, events, functions, types, notifications, states = [], [], [], [], [], []
    for filename, destination in [("simulation_variables", variables), ("event_ids", events)]:
        root, url = fetch("references/variables/" + filename + ".html")
        category = ""
        for node in root.walk():
            if node.tag in {"h2", "h3", "h4"}:
                category = node.text()
            if node.tag != "table":
                continue
            rows = list(node.walk("tr"))
            header = " ".join(c.text() for c in node.walk("th"))
            expected = "Simulation Variable" if destination is variables else "Event ID"
            if not header.startswith(expected):
                continue
            for row in rows:
                cells = [c.text() for c in row.children if isinstance(c, Node) and c.tag in {"td", "th"}]
                if any(c.tag == "th" for c in row.children if isinstance(c, Node)):
                    continue
                assert len(cells) in ({5} if destination is variables else {4}), (category, cells)
                if destination is variables:
                    name, description, units, settable, multiplayer = cells
                    if not any(cells[1:]):
                        continue  # A subsection label, not a variable.
                    assert settable == "N" or settable.startswith("Y"), (name, settable)
                    destination.append(dict(category=category, name=name, units=units,
                                            settable=settable, readable="N" if "set only" in settable.lower() or name == "AI WAYPOINT LIST" else "Y",
                                            multiplayer=multiplayer,
                                            deprecated="DEPRECATED" in description.upper(), source=url))
                else:
                    key, name, description, multiplayer = cells
                    if not name and not description and not multiplayer:
                        continue
                    destination.append(dict(category=category, event_id=key, name=name,
                                            documented_supported=name.lower() != "unsupported",
                                            multiplayer=multiplayer, source=url))

    globals_ = []
    sample, sample_url = fetch("simconnect_api/samples/data_harvester.html")
    sample_text = sample.text()
    for filename, prefix in [("environment_variables", "E"), ("program_variables", "P")]:
        root, url = fetch("references/variables/" + filename + ".html")
        for table in root.walk("table"):
            header = " ".join(c.text() for c in table.walk("th"))
            if not header.startswith("Variable"):
                continue
            cells = [c.text() for c in table.walk("td")]
            assert len(cells) % 5 == 0
            for i in range(0, len(cells), 5):
                name, _, units, settable, multiplayer = cells[i:i + 5]
                assert settable in {"Y", "N"}
                globals_.append(dict(prefix=prefix, name=name, units=units, settable=settable,
                                     in_simconnect_sample=('"' + name + '"') in sample_text,
                                     source=url, sample_source=sample_url))

    root, index_url = fetch("simconnect_api/references/simconnect_references.html")
    for table in root.walk("table"):
        for row in table.walk("tr"):
            cells = [c for c in row.children if isinstance(c, Node) and c.tag in {"td", "th"}]
            if not cells:
                continue
            name = cells[0].text()
            link = next(cells[0].walk("a"), None)
            if name.startswith("SimConnect_") and link:
                entry = dict(name=name, source=urljoin(index_url, link.attrs["href"]))
                if entry not in functions:
                    functions.append(entry)
            elif name.startswith("SIMCONNECT_") and link:
                types.append(dict(name=name, kind=cells[1].text(), source=urljoin(index_url, link.attrs["href"])))

    pages = {}
    documented_functions = set()
    for url in sorted({r["source"].split("#")[0] for r in functions}):
        page, _ = fetch(url)
        pages[url] = page
        documented_functions.update(n.text() for n in page.walk("h3") if n.text().startswith("SimConnect_"))
    general_url = urljoin(BASE, "simconnect_api/references/general_functions.html")
    general = pages[general_url]
    for function in functions:
        function["group"] = function["source"].split("/")[-1].split("#")[0].replace("_functions.html", "")
        function["detail_heading_found"] = function["name"] in documented_functions
        function["parameters"] = ""
        section, part = "", ""
        page = pages[function["source"].split("#")[0]]
        names = []
        for node in page.walk():
            if node.tag == "h3":
                section = node.text()
            elif node.tag == "h4":
                part = node.text()
            elif node.tag == "dt" and section.casefold() == function["name"].casefold() and part == "Parameters":
                names.append(node.text())
        function["parameters"] = "; ".join(names)
    section = ""
    for node in general.walk():
        if node.tag == "h3":
            section = node.text()
        if node.tag != "table":
            continue
        rows = list(node.walk("tr"))
        header = " ".join(c.text() for c in node.walk("th"))
        target = notifications if "System Event Name" in header else states if section == "SimConnect_RequestSystemState" and header.startswith("String") else None
        if target is not None:
            # The SDK's ObjectAdded cells lack a surrounding tr; read the two-column cells.
            cells = [c.text() for c in node.walk("td")]
            assert len(cells) % 2 == 0
            for i in range(0, len(cells), 2):
                if cells[i]:
                    target.append(dict(name=cells[i], source=general_url + "#" + section))

    # These known entries make a changed SDK layout fail instead of silently producing empty catalogs.
    assert any(r["name"] == "PLANE LATITUDE" and r["settable"] == "Y" for r in variables)
    assert any(r["name"] == "GPS POSITION LAT" and r["settable"] == "N" for r in variables)
    assert any(r["name"] == "AXIS_AILERONS_SET" for r in events)
    assert any(r["name"] == "SimConnect_RegisterExternalSim" for r in functions)
    assert any(r["name"] == "Frame" for r in notifications)
    assert any(r["name"] == "ObjectAdded" for r in notifications)
    assert any(r["name"] == "FlightPlan" for r in states)
    assert len(variables) > 500 and len(events) > 500 and len(functions) > 100
    save("simvars", variables, ["category", "name", "units", "settable", "readable", "multiplayer", "deprecated", "source"])
    save("globals", globals_, ["prefix", "name", "units", "settable", "in_simconnect_sample", "source", "sample_source"])
    save("events", events, ["category", "event_id", "name", "documented_supported", "multiplayer", "source"])
    save("functions", functions, ["group", "name", "parameters", "detail_heading_found", "source"])
    save("types", types, ["name", "kind", "source"])
    save("system-events", notifications, ["name", "source"])
    save("system-states", states, ["name", "source"])
    summary = dict(collected_utc=datetime.now(timezone.utc).isoformat(), sdk="Prepar3D v6 public SDK",
                   counts={"simvar_rows": len(variables), "simvar_unique_names": len({r["name"] for r in variables}),
                           "settable_rows": sum(r["settable"].startswith("Y") for r in variables),
                           "write_only_rows": sum(r["readable"] == "N" for r in variables),
                           "global_rows": len(globals_),
                           "global_rows_in_simconnect_sample": sum(r["in_simconnect_sample"] for r in globals_),
                           "event_rows": len(events), "event_unique_names": len({r["name"] for r in events}),
                           "event_supported_unique_names": len({r["name"] for r in events if r["documented_supported"]}),
                           "functions": len(functions), "types": len(types),
                           "system_events": len(notifications), "system_states": len(states)},
                   variable_categories=dict(Counter(r["category"] for r in variables)),
                   event_categories=dict(Counter(r["category"] for r in events)), sources=sources,
                   detail_functions_not_in_index=sorted(documented_functions - {r["name"] for r in functions}),
                   index_functions_without_detail_heading=[r["name"] for r in functions if not r["detail_heading_found"]],
                   validation="Passed: source table row shape, Y/N domain, known entries, nonempty catalogs. No simulator runtime test.")
    (OUT / "inventory.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(summary, ensure_ascii=False, indent=2))


if __name__ == "__main__":
    collect()
