#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
SilkTex - Document Statistics module

This module provides statistics about LaTeX documents including
word count, character count, and document structure analysis.
"""

import re
import gi

gi.require_version('Gtk', '4.0')
gi.require_version('Adw', '1')
from gi.repository import Gtk, Adw, GObject


class StatisticsManager:
    """Manager for document statistics"""
    
    def __init__(self):
        """Initialize the statistics manager"""
        # Regular expressions for counting different elements
        self.word_regex = re.compile(r'\w+')
        self.command_regex = re.compile(r'\\[a-zA-Z]+')
        self.environment_regex = re.compile(r'\\begin\{([^}]+)\}.*?\\end\{\1\}', re.DOTALL)
        self.figure_regex = re.compile(r'\\begin\{figure\}.*?\\end\{figure\}', re.DOTALL)
        self.table_regex = re.compile(r'\\begin\{table\}.*?\\end\{table\}', re.DOTALL)
        self.math_inline_regex = re.compile(r'\$[^$]+\$')
        self.math_display_regex = re.compile(r'\$\$.*?\$\$|\\\[.*?\\\]|\\begin\{equation\}.*?\\end\{equation\}', re.DOTALL)
        self.citation_regex = re.compile(r'\\cite(?:\[[^]]*\])?\{[^}]+\}')
        self.label_regex = re.compile(r'\\label\{[^}]+\}')
        self.reference_regex = re.compile(r'\\ref\{[^}]+\}')
        
        # Structure elements
        self.section_regex = re.compile(r'\\section\{([^}]+)\}')
        self.subsection_regex = re.compile(r'\\subsection\{([^}]+)\}')
        self.subsubsection_regex = re.compile(r'\\subsubsection\{([^}]+)\}')
        self.chapter_regex = re.compile(r'\\chapter\{([^}]+)\}')
    
    def analyze_document(self, text):
        """Analyze LaTeX text and return statistics
        
        Args:
            text: LaTeX document text
            
        Returns:
            dict: Dictionary with statistics
        """
        # Remove comments
        text_without_comments = self._remove_comments(text)
        
        # Get word count excluding commands
        word_count = len(self.word_regex.findall(text_without_comments))
        
        # Count LaTeX elements
        commands = self.command_regex.findall(text)
        environments = self.environment_regex.findall(text)
        figures = self.figure_regex.findall(text)
        tables = self.table_regex.findall(text)
        math_inline = self.math_inline_regex.findall(text)
        math_display = self.math_display_regex.findall(text)
        citations = self.citation_regex.findall(text)
        labels = self.label_regex.findall(text)
        references = self.reference_regex.findall(text)
        
        # Count structural elements
        sections = self.section_regex.findall(text)
        subsections = self.subsection_regex.findall(text)
        subsubsections = self.subsubsection_regex.findall(text)
        chapters = self.chapter_regex.findall(text)
        
        # Character statistics
        chars_with_spaces = len(text)
        chars_without_spaces = len(text.replace(' ', '').replace('\n', '').replace('\t', ''))
        
        # Line statistics
        lines = text.splitlines()
        line_count = len(lines)
        empty_lines = sum(1 for line in lines if not line.strip())
        
        # Create statistics dictionary
        stats = {
            "text": {
                "words": word_count,
                "characters": chars_without_spaces,
                "characters_with_spaces": chars_with_spaces,
                "lines": line_count,
                "empty_lines": empty_lines,
            },
            "structure": {
                "chapters": len(chapters),
                "sections": len(sections),
                "subsections": len(subsections),
                "subsubsections": len(subsubsections),
            },
            "elements": {
                "figures": len(figures),
                "tables": len(tables),
                "math_inline": len(math_inline),
                "math_display": len(math_display),
                "citations": len(citations),
                "labels": len(labels),
                "references": len(references),
                "environments": len(environments),
                "commands": len(commands),
            }
        }
        
        return stats
    
    def _remove_comments(self, text):
        """Remove LaTeX comments from text
        
        Args:
            text: LaTeX text
            
        Returns:
            str: Text with comments removed
        """
        lines = []
        for line in text.splitlines():
            comment_pos = line.find('%')
            if comment_pos >= 0:
                # Check if the % is escaped
                if comment_pos == 0 or line[comment_pos-1] != '\\':
                    line = line[:comment_pos]
            lines.append(line)
        return '\n'.join(lines)


class StatisticsDialog(Adw.Dialog):
    """Dialog for displaying document statistics"""
    
    def __init__(self, parent_window, text):
        """Initialize the statistics dialog
        
        Args:
            parent_window: Parent window
            text: LaTeX document text to analyze
        """
        super().__init__()
        
        self.parent_window = parent_window
        self.text = text
        
        # Create statistics manager and analyze document
        self.stats_manager = StatisticsManager()
        self.stats = self.stats_manager.analyze_document(text)
        
        # Set up dialog properties
        self.set_title("Document Statistics")
        self.set_content_width(480)
        self.set_content_height(600)
        
        self.create_ui()
    
    def create_ui(self):
        """Create the dialog UI"""
        # Main content box
        content_box = Gtk.Box(orientation=Gtk.Orientation.VERTICAL)
        content_box.set_margin_top(24)
        content_box.set_margin_bottom(24)
        content_box.set_margin_start(24)
        content_box.set_margin_end(24)
        content_box.set_spacing(24)
        
        # Add statistics groups
        text_stats = Adw.PreferencesGroup()
        text_stats.set_title("Text Statistics")
        
        # Add word count
        word_row = Adw.ActionRow()
        word_row.set_title("Words")
        word_label = Gtk.Label.new(str(self.stats["text"]["words"]))
        word_label.add_css_class("dim-label")
        word_row.add_suffix(word_label)
        text_stats.add(word_row)
        
        # Add character count
        char_row = Adw.ActionRow()
        char_row.set_title("Characters (without spaces)")
        char_label = Gtk.Label.new(str(self.stats["text"]["characters"]))
        char_label.add_css_class("dim-label")
        char_row.add_suffix(char_label)
        text_stats.add(char_row)
        
        # Add character with spaces count
        char_spaces_row = Adw.ActionRow()
        char_spaces_row.set_title("Characters (with spaces)")
        char_spaces_label = Gtk.Label.new(str(self.stats["text"]["characters_with_spaces"]))
        char_spaces_label.add_css_class("dim-label")
        char_spaces_row.add_suffix(char_spaces_label)
        text_stats.add(char_spaces_row)
        
        # Add line count
        line_row = Adw.ActionRow()
        line_row.set_title("Lines")
        line_label = Gtk.Label.new(str(self.stats["text"]["lines"]))
        line_label.add_css_class("dim-label")
        line_row.add_suffix(line_label)
        text_stats.add(line_row)
        
        # Add empty line count
        empty_line_row = Adw.ActionRow()
        empty_line_row.set_title("Empty Lines")
        empty_line_label = Gtk.Label.new(str(self.stats["text"]["empty_lines"]))
        empty_line_label.add_css_class("dim-label")
        empty_line_row.add_suffix(empty_line_label)
        text_stats.add(empty_line_row)
        
        content_box.append(text_stats)
        
        # Structure statistics
        structure_stats = Adw.PreferencesGroup()
        structure_stats.set_title("Document Structure")
        
        # Add chapter count
        chapter_row = Adw.ActionRow()
        chapter_row.set_title("Chapters")
        chapter_label = Gtk.Label.new(str(self.stats["structure"]["chapters"]))
        chapter_label.add_css_class("dim-label")
        chapter_row.add_suffix(chapter_label)
        structure_stats.add(chapter_row)
        
        # Add section count
        section_row = Adw.ActionRow()
        section_row.set_title("Sections")
        section_label = Gtk.Label.new(str(self.stats["structure"]["sections"]))
        section_label.add_css_class("dim-label")
        section_row.add_suffix(section_label)
        structure_stats.add(section_row)
        
        # Add subsection count
        subsection_row = Adw.ActionRow()
        subsection_row.set_title("Subsections")
        subsection_label = Gtk.Label.new(str(self.stats["structure"]["subsections"]))
        subsection_label.add_css_class("dim-label")
        subsection_row.add_suffix(subsection_label)
        structure_stats.add(subsection_row)
        
        # Add subsubsection count
        subsubsection_row = Adw.ActionRow()
        subsubsection_row.set_title("Subsubsections")
        subsubsection_label = Gtk.Label.new(str(self.stats["structure"]["subsubsections"]))
        subsubsection_label.add_css_class("dim-label")
        subsubsection_row.add_suffix(subsubsection_label)
        structure_stats.add(subsubsection_row)
        
        content_box.append(structure_stats)
        
        # Elements statistics
        elements_stats = Adw.PreferencesGroup()
        elements_stats.set_title("Document Elements")
        
        # Add figure count
        figure_row = Adw.ActionRow()
        figure_row.set_title("Figures")
        figure_label = Gtk.Label.new(str(self.stats["elements"]["figures"]))
        figure_label.add_css_class("dim-label")
        figure_row.add_suffix(figure_label)
        elements_stats.add(figure_row)
        
        # Add table count
        table_row = Adw.ActionRow()
        table_row.set_title("Tables")
        table_label = Gtk.Label.new(str(self.stats["elements"]["tables"]))
        table_label.add_css_class("dim-label")
        table_row.add_suffix(table_label)
        elements_stats.add(table_row)
        
        # Add math count
        math_row = Adw.ActionRow()
        math_row.set_title("Math Equations")
        math_inline = self.stats["elements"]["math_inline"]
        math_display = self.stats["elements"]["math_display"]
        math_total = math_inline + math_display
        math_label = Gtk.Label.new(f"{math_total} ({math_inline} inline, {math_display} display)")
        math_label.add_css_class("dim-label")
        math_row.add_suffix(math_label)
        elements_stats.add(math_row)
        
        # Add citation count
        citation_row = Adw.ActionRow()
        citation_row.set_title("Citations")
        citation_label = Gtk.Label.new(str(self.stats["elements"]["citations"]))
        citation_label.add_css_class("dim-label")
        citation_row.add_suffix(citation_label)
        elements_stats.add(citation_row)
        
        # Add label count
        label_row = Adw.ActionRow()
        label_row.set_title("Labels")
        label_label = Gtk.Label.new(str(self.stats["elements"]["labels"]))
        label_label.add_css_class("dim-label")
        label_row.add_suffix(label_label)
        elements_stats.add(label_row)
        
        # Add reference count
        reference_row = Adw.ActionRow()
        reference_row.set_title("References")
        reference_label = Gtk.Label.new(str(self.stats["elements"]["references"]))
        reference_label.add_css_class("dim-label")
        reference_row.add_suffix(reference_label)
        elements_stats.add(reference_row)
        
        # Add environment count
        environment_row = Adw.ActionRow()
        environment_row.set_title("Environments")
        environment_label = Gtk.Label.new(str(self.stats["elements"]["environments"]))
        environment_label.add_css_class("dim-label")
        environment_row.add_suffix(environment_label)
        elements_stats.add(environment_row)
        
        # Add command count
        command_row = Adw.ActionRow()
        command_row.set_title("LaTeX Commands")
        command_label = Gtk.Label.new(str(self.stats["elements"]["commands"]))
        command_label.add_css_class("dim-label")
        command_row.add_suffix(command_label)
        elements_stats.add(command_row)
        
        content_box.append(elements_stats)
        
        # Button to close dialog
        button_box = Gtk.Box(orientation=Gtk.Orientation.HORIZONTAL)
        button_box.set_halign(Gtk.Align.END)
        button_box.set_spacing(12)
        
        close_button = Gtk.Button.new_with_label("Close")
        close_button.add_css_class('suggested-action')
        close_button.connect('clicked', self.on_close_clicked)
        button_box.append(close_button)
        
        content_box.append(button_box)
        
        # Set dialog content
        self.set_content(content_box)
    
    def on_close_clicked(self, button):
        """Handle close button click"""
        self.close()
