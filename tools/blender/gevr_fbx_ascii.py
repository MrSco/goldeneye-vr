"""
A minimal ASCII FBX 7.x reader for Meta's controller art (Blender's importer
reads binary FBX only, and three of the four controllers are ASCII): the
node tree, each array as a numpy array, the objects by id, the connections
and the global settings. Used by gevr_touch_export.py.
"""

import os
import re

import numpy as np


class Node:
    __slots__ = ("name", "props", "children", "array")

    def __init__(self, name, props):
        self.name = name
        self.props = props
        self.children = []
        self.array = None

    def find(self, name):
        for c in self.children:
            if c.name == name:
                return c
        return None

    def findall(self, name):
        return [c for c in self.children if c.name == name]


_TOKEN = re.compile(r'"((?:[^"\\]|\\.)*)"|([^,\s]+)')


def _props(text):
    out = []
    for m in _TOKEN.finditer(text):
        if m.group(1) is not None:
            out.append(m.group(1))
            continue
        t = m.group(2)
        try:
            out.append(int(t))
        except ValueError:
            try:
                out.append(float(t))
            except ValueError:
                out.append(t)
    return out


def long_path(p):
    """Windows' 260-character limit trips on the art's names in a deep folder."""
    p = os.path.abspath(p)
    if os.name == "nt" and not p.startswith("\\\\?\\"):
        return "\\\\?\\" + p
    return p


def parse(path):
    root = Node("root", [])
    stack = [root]
    with open(long_path(path), "r", encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    i, n = 0, len(lines)
    while i < n:
        line = lines[i].strip()
        i += 1
        if not line or line.startswith(";"):
            continue
        if line == "}":
            stack.pop()
            continue
        k = line.find(":")
        if k < 0:
            continue
        key, rest = line[:k], line[k + 1:].strip()
        if key == "a":
            # an array's body, over as many lines as it takes
            buf = [rest]
            while i < n and not lines[i].strip().startswith("}"):
                buf.append(lines[i].strip())
                i += 1
            stack[-1].array = np.array([float(x) for x in "".join(buf).split(",") if x], dtype=np.float64)
            continue
        if rest.endswith("{"):
            node = Node(key, _props(rest[:-1]))
            stack[-1].children.append(node)
            stack.append(node)
            continue
        while rest.endswith(",") and i < n:   # a property continued on the next line
            rest += lines[i].strip()
            i += 1
        stack[-1].children.append(Node(key, _props(rest)))
    return root


def props70(node):
    out = {}
    p = node.find("Properties70")
    if p:
        for c in p.findall("P"):
            out[c.props[0]] = c.props[4:]
    return out


def load(path):
    """(root, objects by id, connections as tuples, GlobalSettings properties)."""
    root = parse(path)
    objs = {}
    for c in root.find("Objects").children:
        if c.props and isinstance(c.props[0], int):
            objs[c.props[0]] = c
    conns = []
    cn = root.find("Connections")
    if cn:
        conns = [tuple(c.props) for c in cn.findall("C")]
    gs = root.find("GlobalSettings")
    return root, objs, conns, (props70(gs) if gs else {})
