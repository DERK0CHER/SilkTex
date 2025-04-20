"""
@file snippets.py
@brief Handle snippets for Gummi

Copyright (C) 2009-2025 Gummi Developers
All Rights reserved.

Permission is hereby granted, free of charge, to any person
obtaining a copy of this software and associated documentation
files (the "Software"), to deal in the Software without
restriction, including without limitation the rights to use,
copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the
Software is furnished to do so, subject to the following
conditions:

The above copyright notice and this permission notice shall be
included in all copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
OTHER DEALINGS IN THE SOFTWARE.
"""

import os
import re
import logging
from dataclasses import dataclass
from typing import List, Dict, Tuple, Optional, Any, Union, Callable

import gi
gi.require_version('Gtk', '4.0')
gi.require_version('GtkSource', '5')
from gi.repository import Gtk, Gdk, GtkSource, GObject, GLib

from constants import C_GUMMI_CONFDIR, GUMMI_DATA, DIR_PERMS
from editor import GuEditor
from environment import gummi_get_active_editor
from utils import utils_copy_file, utils_set_file_contents

# Setup logging
logger = logging.getLogger(__name__)


@dataclass
class Tuple2:
    """Simple tuple class to mimic the C implementation's Tuple2 struct."""
    first: Any
    second: Any


@dataclass
class GuSnippetExpandInfo:
    """Information about a snippet expansion."""
    group_number: int = 0
    start: int = 0
    len: int = 0
    text: str = ""
    left_mark: Optional[Gtk.TextMark] = None
    right_mark: Optional[Gtk.TextMark] = None


class GuSnippetInfo:
    """Information about a snippet."""
    
    def __init__(self, snippet: str):
        self.snippet = snippet
        self.expanded = snippet
        self.einfo: List[GuSnippetExpandInfo] = []
        self.einfo_sorted: List[GuSnippetExpandInfo] = []
        self.einfo_unique: List[GuSnippetExpandInfo] = []
        self.current = None
        self.start_offset = 0
        self.sel_text = ""
        self.sel_start = None
        self.offset = 0


class GuSnippets:
    """Class to handle snippets for the Gummi editor."""
    
    def __init__(self):
        """Initialize a new GuSnippets instance."""
        filename = os.path.join(C_GUMMI_CONFDIR, "snippets.cfg")
        dirname = os.path.dirname(filename)
        os.makedirs(dirname, mode=DIR_PERMS, exist_ok=True)
        
        logger.info(f"Snippets: {filename}")
        self.filename = filename
        self.accel_group = Gtk.AccelGroup.new()
        self.stackframe = None
        self.head = None
        self.info = None
        self.closure_data = []
        
        self.load()
    
    def set_default(self):
        """Reset snippets to default configuration."""
        err = None
        snip = os.path.join(GUMMI_DATA, "snippets", "snippets.cfg")
        
        # Remove all accelerators
        for closure_data in self.closure_data:
            self.accel_group.disconnect(closure_data.second)
            logger.debug(f"Accelerator for '{closure_data.first}' disconnected")
        
        self.closure_data = []
        
        try:
            utils_copy_file(snip, self.filename)
            self.load()
        except Exception as e:
            logger.error("Can't open snippets file for writing, snippets may not work properly")
            err = e
    
    def load(self):
        """Load snippets from the config file."""
        if self.head:
            self.clean_up()
        
        try:
            with open(self.filename, "r") as f:
                lines = f.readlines()
        except FileNotFoundError:
            logger.error("Can't find snippets file, resetting to default")
            self.set_default()
            return
        
        current = None
        prev = None
        self.head = None
        
        for line in lines:
            line = line.rstrip('\n')
            current = {"first": None, "second": None, "next": None}
            
            if not self.head:
                self.head = current
                prev = current
            else:
                prev["next"] = current
            
            if not line.startswith('\t'):
                if line.startswith('#') or not line:
                    current["first"] = line
                else:
                    parts = line.split(" ", 1)
                    seg = parts[1] if len(parts) > 1 else ""
                    current["first"] = seg if seg else "Invalid"
                    self.set_accelerator(current["first"])
            else:
                if not prev["second"]:
                    prev["second"] = line[1:]
                    continue
                
                rot = prev["second"]
                prev["second"] = rot + "\n" + line[1:]
                current = prev
            
            prev = current
        
        if prev:
            prev["next"] = None
    
    def save(self):
        """Save snippets to the config file."""
        current = self.head
        
        try:
            with open(self.filename, "w") as f:
                while current:
                    # Skip comments
                    if not current["second"]:
                        f.write(f"{current['first']}\n")
                        current = current["next"]
                        continue
                    
                    f.write(f"snippet {current['first']}\n\t")
                    
                    # Replace '\n' with '\n\t' for options with multi-line content
                    content = current["second"].replace('\n', '\n\t')
                    f.write(f"{content}\n")
                    
                    current = current["next"]
        except Exception as e:
            logger.error(f"Can't open snippets file for writing: {e}")
            raise
    
    def clean_up(self):
        """Free resources used by the snippets."""
        prev = self.head
        while prev:
            current = prev["next"]
            prev = current
        
        self.head = None
    
    def get_value(self, term: str) -> Optional[str]:
        """Get the value for a snippet term.
        
        Args:
            term: The snippet term to look up
            
        Returns:
            The snippet value or None if not found
        """
        key = f"{term},"
        index = self._slist_find(self.head, key, True, False)
        return index["second"] if index else None
    
    def _slist_find(self, head, key, case_sensitive=True, partial=False):
        """Find a key in the linked list.
        
        Args:
            head: The head of the list
            key: The key to search for
            case_sensitive: Whether to do a case-sensitive comparison
            partial: Whether to do a partial match
            
        Returns:
            The node containing the key or None if not found
        """
        current = head
        while current:
            if current["first"]:
                found = False
                
                if partial:
                    if case_sensitive:
                        found = key in current["first"]
                    else:
                        found = key.lower() in current["first"].lower()
                else:
                    if case_sensitive:
                        found = key == current["first"]
                    else:
                        found = key.lower() == current["first"].lower()
                
                if found:
                    return current
            
            current = current["next"]
        
        return None
    
    def set_accelerator(self, config: str):
        """Set an accelerator for a snippet.
        
        Args:
            config: Configuration string with the form: Key,Accel_key,Name
        """
        configs = config.split(",", 2)
        
        # Return if configs does not contain accelerator
        if len(configs) < 2 or not configs[1]:
            return
        
        data = Tuple2(self, configs[0])
        closure = GObject.Closure.new_simple(GObject.Callback(self.accel_cb), data)
        closure_data = Tuple2(data.second, closure)
        self.closure_data.append(closure_data)
        
        key, mod = Gtk.accelerator_parse(configs[1])
        
        # Return without connect if accel is not valid
        if not Gtk.accelerator_valid(key, mod):
            return
        
        self.accel_connect(key, mod, closure)
    
    def activate(self, editor: GuEditor, key: str):
        """Activate a snippet.
        
        Args:
            editor: The editor to activate the snippet in
            key: The snippet key
        """
        logger.debug(f"Snippet '{key}' activated")
        
        snippet = self.get_value(key)
        if not snippet:
            return
        
        new_info = self.parse(snippet)
        
        buffer = editor.buffer
        bounds = buffer.get_selection_bounds()
        if bounds:
            start, end = bounds
        else:
            cursor = buffer.get_insert()
            start = buffer.get_iter_at_mark(cursor)
            end = start.copy()
        
        new_info.start_offset = start.get_offset()
        new_info.sel_text = buffer.get_text(start, end, True)
        
        marks = start.get_marks()
        if marks:
            new_info.sel_start = marks[0]
        
        buffer.insert(start, new_info.expanded, -1)
        self.snippet_info_create_marks(new_info, editor)
        self.snippet_info_initial_expand(new_info, editor)
        buffer.set_modified(True)
        
        if self.info:
            self.snippet_info_sync_group(self.info, editor)
            if not self.stackframe:
                self.stackframe = []
            self.stackframe.append(self.info)
        
        self.info = new_info
        
        if not self.snippet_info_goto_next_placeholder(self.info, editor):
            self.deactivate(editor)
    
    def deactivate(self, editor: GuEditor):
        """Deactivate the current snippet.
        
        Args:
            editor: The editor containing the snippet
        """
        self.snippet_info_free(self.info, editor)
        
        if self.stackframe:
            last = self.stackframe[-1] if self.stackframe else None
            if last:
                self.info = last
                self.stackframe.remove(last)
        else:
            self.info = None
        
        logger.debug("Snippet deactivated")
    
    def key_press_cb(self, editor: GuEditor, event: Gdk.EventKey) -> bool:
        """Handle key press events for snippets.
        
        Args:
            editor: The editor
            event: The key event
            
        Returns:
            True if the event was handled, False otherwise
        """
        buffer = editor.buffer
        
        if event.keyval == Gdk.KEY_Tab:
            cursor = buffer.get_insert()
            current = buffer.get_iter_at_mark(cursor)
            
            if current.ends_word():
                start = current.copy()
                start.backward_word_start()
                key = buffer.get_text(start, current, True)
                
                if self.get_value(key):
                    buffer.delete(start, current)
                    self.activate(editor, key)
                    return True
        
        if self.info:
            if event.keyval == Gdk.KEY_Tab:
                if not self.snippet_info_goto_next_placeholder(self.info, editor):
                    self.deactivate(editor)
                return True
            elif event.keyval == Gdk.KEY_ISO_Left_Tab and event.state & Gdk.ModifierType.SHIFT_MASK:
                if not self.snippet_info_goto_prev_placeholder(self.info, editor):
                    self.deactivate(editor)
                return True
            
            # Deactivate snippet if the current insert range is not within the snippet
            cursor = buffer.get_insert()
            current = buffer.get_iter_at_mark(cursor)
            offset = current.get_offset()
            
            if self.info.einfo:
                last = self.info.einfo[-1]
                current = buffer.get_iter_at_mark(last.left_mark)
                bound_end = current.get_offset()
                
                if offset < self.info.start_offset or offset > bound_end:
                    self.deactivate(editor)
        
        return False
    
    def key_release_cb(self, editor: GuEditor, event: Gdk.EventKey) -> bool:
        """Handle key release events for snippets.
        
        Args:
            editor: The editor
            event: The key event
            
        Returns:
            True if the event was handled, False otherwise
        """
        if event.keyval != Gdk.KEY_Tab and not self.info:
            return False
        
        if self.info:
            self.snippet_info_sync_group(self.info, editor)
        
        return False
    
    def parse(self, snippet: str) -> GuSnippetInfo:
        """Parse a snippet string.
        
        Args:
            snippet: The snippet string
            
        Returns:
            A GuSnippetInfo object
        """
        info = GuSnippetInfo(snippet)
        
        holders = [
            r"\$([0-9]+)",
            r"\${([0-9]*):?([^}]*)}",
            r"\$(FILENAME)",
            r"\${(FILENAME)}",
            r"\$(BASENAME)",
            r"\${(BASENAME)}",
            r"\$(SELECTED_TEXT)",
            r"\${(SELECTED_TEXT)}"
        ]
        
        for pattern in holders:
            matches = re.finditer(pattern, snippet, re.DOTALL)
            
            for match in matches:
                groups = match.groups()
                start, end = match.span(0)
                
                # Convert start, end to UTF-8 offset
                s_start = snippet[:start]
                s_end = snippet[:end]
                start = len(s_start)
                end = len(s_end)
                
                if pattern in holders[:2]:  # Numeric placeholders
                    self.snippet_info_append_holder(info, int(groups[0]), start, end - start, groups[1] if len(groups) > 1 else "")
                else:  # Special placeholders
                    self.snippet_info_append_holder(info, -1, start, end - start, groups[0])
                
                logger.debug(f"Placeholder: {match.group(0)}, {groups[0]}, {groups[1] if len(groups) > 1 else ''}")
        
        info.einfo.sort(key=lambda x: x.start)
        info.einfo_sorted = sorted(info.einfo, key=lambda x: x.group_number)
        
        return info
    
    def accel_cb(self, accel_group: Gtk.AccelGroup, obj: GObject.Object, keyval: int, mods: Gdk.ModifierType, udata: Tuple2):
        """Handle accelerator callback.
        
        Args:
            accel_group: The accelerator group
            obj: The object that activated the accelerator
            keyval: The key value
            mods: The modifiers
            udata: User data
        """
        sc = udata.first
        key = udata.second
        
        # We need to get the active editor here
        sc.activate(gummi_get_active_editor(), key)
    
    def accel_connect(self, keyval: int, mod: Gdk.ModifierType, closure: GObject.Closure):
        """Connect an accelerator.
        
        Args:
            keyval: The key value
            mod: The modifiers
            closure: The closure to call
        """
        self.accel_group.connect(
            keyval,
            Gtk.accelerator_get_default_mod_mask() & mod,
            Gtk.AccelFlags.VISIBLE,
            closure
        )
        
        acc = Gtk.accelerator_get_label(keyval, Gtk.accelerator_get_default_mod_mask() & mod)
        logger.debug(f"Accelerator '{acc}' connected")
    
    def accel_disconnect(self, key: str):
        """Disconnect an accelerator.
        
        Args:
            key: The key to disconnect
        """
        if not key:
            return
        
        for closure_data in self.closure_data:
            if closure_data.first == key:
                self.accel_group.disconnect(closure_data.second)
                self.closure_data.remove(closure_data)
                logger.debug(f"Accelerator for '{closure_data.first}' disconnected")
                break
    
    def snippet_info_append_holder(self, info: GuSnippetInfo, group: int, start: int, length: int, text: str):
        """Append a holder to a snippet info.
        
        Args:
            info: The snippet info
            group: The group number
            start: The start offset
            length: The length
            text: The text
        """
        einfo = GuSnippetExpandInfo()
        einfo.group_number = group
        einfo.start = start
        einfo.len = length
        einfo.text = text or ""
        info.einfo.append(einfo)
    
    def snippet_info_create_marks(self, info: GuSnippetInfo, editor: GuEditor):
        """Create marks for a snippet info.
        
        Args:
            info: The snippet info
            editor: The editor
        """
        buffer = editor.buffer
        
        for einfo in info.einfo:
            start = buffer.get_iter_at_offset(info.start_offset + einfo.start)
            end = buffer.get_iter_at_offset(info.start_offset + einfo.start + einfo.len)
            
            einfo.left_mark = buffer.create_mark(None, start, True)
            einfo.right_mark = buffer.create_mark(None, end, False)
        
        logger.debug("Marks created")
    
    def snippet_info_remove_marks(self, info: GuSnippetInfo, editor: GuEditor):
        """Remove marks for a snippet info.
        
        Args:
            info: The snippet info
            editor: The editor
        """
        buffer = editor.buffer
        
        for einfo in info.einfo:
            buffer.delete_mark(einfo.left_mark)
            buffer.delete_mark(einfo.right_mark)
        
        logger.debug("Marks removed")
    
    def snippet_info_initial_expand(self, info: GuSnippetInfo, editor: GuEditor):
        """Initially expand a snippet.
        
        Args:
            info: The snippet info
            editor: The editor
        """
        buffer = editor.buffer
        
        # Map group numbers to their leader
        group_map = {}
        
        for einfo in info.einfo:
            if einfo.group_number not in group_map:
                group_map[einfo.group_number] = einfo
                info.einfo_unique.append(einfo)
        
        info.einfo_unique.sort(key=lambda x: x.group_number)
        
        info.offset = 0
        
        for einfo in info.einfo:
            value = group_map.get(einfo.group_number)
            
            start = buffer.get_iter_at_mark(einfo.left_mark)
            end = buffer.get_iter_at_mark(einfo.right_mark)
            
            # Expand macros
            text = einfo.text
            
            if text == "SELECTED_TEXT":
                buffer.delete(start, end)
                buffer.insert(start, info.sel_text, -1)
                
                if info.sel_start:
                    ms = buffer.get_iter_at_mark(info.sel_start)
                    me = ms.copy()
                    me.forward_chars(len(info.sel_text))
                    buffer.delete(ms, me)
            
            elif text == "FILENAME":
                buffer.delete(start, end)
                buffer.insert(start, editor.filename or "", -1)
            
            elif text == "BASENAME":
                buffer.delete(start, end)
                basename = os.path.basename(editor.filename or "")
                buffer.insert(start, basename, -1)
            
            else:
                # Expand text of same group with text of group leader
                buffer.delete(start, end)
                buffer.insert(start, value.text, -1)
    
    def snippet_info_sync_group(self, info: GuSnippetInfo, editor: GuEditor):
        """Synchronize a snippet group.
        
        Args:
            info: The snippet info
            editor: The editor
        """
        if not info.current or info.current.group_number == -1:
            return
        
        buffer = editor.buffer
        active = info.current
        
        start = buffer.get_iter_at_mark(active.left_mark)
        end = buffer.get_iter_at_mark(active.right_mark)
        text = buffer.get_text(start, end, True)
        
        for einfo in info.einfo:
            if einfo != active and einfo.group_number == active.group_number:
                start = buffer.get_iter_at_mark(einfo.left_mark)
                end = buffer.get_iter_at_mark(einfo.right_mark)
                buffer.delete(start, end)
                buffer.insert(start, text, -1)
    
    def snippet_info_goto_next_placeholder(self, info: GuSnippetInfo, editor: GuEditor) -> bool:
        """Go to the next placeholder in a snippet.
        
        Args:
            info: The snippet info
            editor: The editor
            
        Returns:
            True if there are more placeholders, False otherwise
        """
        buffer = editor.buffer
        success = True
        
        # Snippet just activated
        if not info.current:
            # Skip $0, $-1 and jump to next placeholder
            if info.einfo_unique:
                info.current = 0
                while info.current < len(info.einfo_unique) and info.einfo_unique[info.current].group_number <= 0:
                    info.current += 1
                
                if info.current >= len(info.einfo_unique):
                    info.current = None
        else:
            info.current += 1
            if info.current >= len(info.einfo_unique):
                info.current = None
        
        # No placeholder left
        if info.current is None:
            # Find $0
            for i, einfo in enumerate(info.einfo_sorted):
                if einfo.group_number == 0:
                    info.current = i
                    break
            
            if info.current is None:
                return False
            else:
                # This is the last one ($0) set to false to deactivate snippet
                success = False
        
        einfo = info.einfo_unique[info.current]
        start = buffer.get_iter_at_mark(einfo.left_mark)
        end = buffer.get_iter_at_mark(einfo.right_mark)
        
        buffer.place_cursor(start)
        buffer.select_range(start, end)
        
        return success
    
    def snippet_info_goto_prev_placeholder(self, info: GuSnippetInfo, editor: GuEditor) -> bool:
        """Go to the previous placeholder in a snippet.
        
        Args:
            info: The snippet info
            editor: The editor
            
        Returns:
            True if there are more placeholders, False otherwise
        """
        buffer = editor.buffer
        
        if info.current is None or info.current <= 0:
            return False
        
        info.current -= 1
        
        # Return false to deactivate snippet
        if info.current < 0 or info.einfo_unique[info.current].group_number < 0:
            return False
        
        einfo = info.einfo_unique[info.current]
        start = buffer.get_iter_at_mark(einfo.left_mark)
        end = buffer.get_iter_at_mark(einfo.right_mark)
        
        buffer.place_cursor(start)
        buffer.select_range(start, end)
        
        return True
    
    def snippet_info_free(self, info: GuSnippetInfo, editor: GuEditor):
        """Free a snippet info.
        
        Args:
            info: The snippet info
            editor: The editor
        """
        if not info:
            return
            
        self.snippet_info_remove_marks(info, editor)